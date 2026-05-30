#!/usr/bin/env swift
import AVFoundation
import CoreGraphics
import Foundation
import ImageIO

struct FrameEntry: Decodable {
	let decodeTime: Int64
	let duration: Int64
	let width: Int
	let height: Int
	let path: String
}

struct FrameManifest: Decodable {
	let width: Int
	let height: Int
	let timeScale: Int32
	let frames: [FrameEntry]
}

func fail(_ message: String) -> Never {
	FileHandle.standardError.write((message + "\n").data(using: .utf8)!)
	exit(1)
}

func usage() -> Never {
	fail("usage: encode_quicktime_from_frames.swift <package-root> <frames.json> <output.mov> [audio.wav]")
}

func cgImage(at url: URL) -> CGImage? {
	guard let source = CGImageSourceCreateWithURL(url as CFURL, nil) else {
		return nil
	}
	return CGImageSourceCreateImageAtIndex(source, 0, nil)
}

func makePixelBuffer(from image: CGImage, width: Int, height: Int) -> CVPixelBuffer? {
	let attrs: [String: Any] = [
		kCVPixelBufferCGImageCompatibilityKey as String: true,
		kCVPixelBufferCGBitmapContextCompatibilityKey as String: true,
	]
	var pixelBuffer: CVPixelBuffer?
	let status = CVPixelBufferCreate(
		kCFAllocatorDefault,
		width,
		height,
		kCVPixelFormatType_32BGRA,
		attrs as CFDictionary,
		&pixelBuffer)
	guard status == kCVReturnSuccess, let buffer = pixelBuffer else {
		return nil
	}
	CVPixelBufferLockBaseAddress(buffer, [])
	defer { CVPixelBufferUnlockBaseAddress(buffer, []) }
	guard
		let base = CVPixelBufferGetBaseAddress(buffer),
		let context = CGContext(
			data: base,
			width: width,
			height: height,
			bitsPerComponent: 8,
			bytesPerRow: CVPixelBufferGetBytesPerRow(buffer),
			space: CGColorSpaceCreateDeviceRGB(),
			bitmapInfo: CGImageAlphaInfo.premultipliedFirst.rawValue | CGBitmapInfo.byteOrder32Little.rawValue)
	else {
		return nil
	}
	context.clear(CGRect(x: 0, y: 0, width: width, height: height))
	context.draw(image, in: CGRect(x: 0, y: 0, width: width, height: height))
	return buffer
}

func resolvePath(_ path: String, relativeTo packageRoot: URL) -> URL {
	if path.hasPrefix("/") {
		return URL(fileURLWithPath: path).standardizedFileURL
	}
	return URL(fileURLWithPath: path, relativeTo: packageRoot).standardizedFileURL
}

func finishExport(_ exporter: AVAssetExportSession) {
	let semaphore = DispatchSemaphore(value: 0)
	exporter.exportAsynchronously {
		semaphore.signal()
	}
	semaphore.wait()
	guard exporter.status == .completed else {
		fail("audio/video mux failed: \(exporter.error?.localizedDescription ?? "unknown error")")
	}
}

func mux(videoURL: URL, audioURL: URL, outputURL: URL) {
	let composition = AVMutableComposition()
	let videoAsset = AVURLAsset(url: videoURL)
	let audioAsset = AVURLAsset(url: audioURL)
	guard let sourceVideo = videoAsset.tracks(withMediaType: .video).first else {
		fail("video-only movie has no video track: \(videoURL.path)")
	}
	guard let sourceAudio = audioAsset.tracks(withMediaType: .audio).first else {
		fail("audio file has no audio track: \(audioURL.path)")
	}
	guard
		let videoTrack = composition.addMutableTrack(
			withMediaType: .video,
			preferredTrackID: kCMPersistentTrackID_Invalid),
		let audioTrack = composition.addMutableTrack(
			withMediaType: .audio,
			preferredTrackID: kCMPersistentTrackID_Invalid)
	else {
		fail("could not create mux composition tracks")
	}
	do {
		try videoTrack.insertTimeRange(
			CMTimeRange(start: .zero, duration: videoAsset.duration),
			of: sourceVideo,
			at: .zero)
		try audioTrack.insertTimeRange(
			CMTimeRange(start: .zero, duration: min(videoAsset.duration, audioAsset.duration)),
			of: sourceAudio,
			at: .zero)
	} catch {
		fail("could not build mux composition: \(error)")
	}
	videoTrack.preferredTransform = sourceVideo.preferredTransform
	try? FileManager.default.removeItem(at: outputURL)
	guard let exporter = AVAssetExportSession(asset: composition, presetName: AVAssetExportPresetHighestQuality) else {
		fail("could not create mux exporter")
	}
	exporter.outputURL = outputURL
	exporter.outputFileType = .mov
	finishExport(exporter)
}

let args = CommandLine.arguments
guard args.count == 4 || args.count == 5 else {
	usage()
}

let packageRoot = URL(fileURLWithPath: args[1], isDirectory: true)
let manifestURL = resolvePath(args[2], relativeTo: packageRoot)
let outputURL = URL(fileURLWithPath: args[3]).standardizedFileURL
let audioURL = args.count == 5 ? resolvePath(args[4], relativeTo: packageRoot) : nil
let videoOnlyURL = audioURL == nil ?
	outputURL :
	outputURL.deletingPathExtension().appendingPathExtension("video-only.tmp.mov")

let manifestData: Data
do {
	manifestData = try Data(contentsOf: manifestURL)
} catch {
	fail("could not read manifest: \(manifestURL.path): \(error)")
}

let manifest: FrameManifest
do {
	manifest = try JSONDecoder().decode(FrameManifest.self, from: manifestData)
} catch {
	fail("could not parse manifest: \(manifestURL.path): \(error)")
}

guard manifest.width > 0, manifest.height > 0, manifest.timeScale > 0, !manifest.frames.isEmpty else {
	fail("manifest has no encodable frames")
}

try? FileManager.default.createDirectory(
	at: outputURL.deletingLastPathComponent(),
	withIntermediateDirectories: true)
try? FileManager.default.removeItem(at: outputURL)
if videoOnlyURL != outputURL {
	try? FileManager.default.removeItem(at: videoOnlyURL)
}

let writer: AVAssetWriter
do {
	writer = try AVAssetWriter(outputURL: videoOnlyURL, fileType: .mov)
} catch {
	fail("could not create AVAssetWriter: \(error)")
}

let input = AVAssetWriterInput(mediaType: .video, outputSettings: [
	AVVideoCodecKey: AVVideoCodecType.h264,
	AVVideoWidthKey: manifest.width,
	AVVideoHeightKey: manifest.height,
])
input.expectsMediaDataInRealTime = false

let adaptor = AVAssetWriterInputPixelBufferAdaptor(
	assetWriterInput: input,
	sourcePixelBufferAttributes: [
		kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA,
		kCVPixelBufferWidthKey as String: manifest.width,
		kCVPixelBufferHeightKey as String: manifest.height,
	])

guard writer.canAdd(input) else {
	fail("writer cannot add H.264 video input")
}
writer.add(input)

guard writer.startWriting() else {
	fail("writer failed to start: \(writer.error?.localizedDescription ?? "unknown error")")
}
writer.startSession(atSourceTime: .zero)

for frame in manifest.frames {
	while !input.isReadyForMoreMediaData {
		Thread.sleep(forTimeInterval: 0.01)
	}
	let frameURL = URL(fileURLWithPath: frame.path, relativeTo: packageRoot).standardizedFileURL
	guard let image = cgImage(at: frameURL) else {
		fail("could not read frame image: \(frameURL.path)")
	}
	guard let pixelBuffer = makePixelBuffer(from: image, width: manifest.width, height: manifest.height) else {
		fail("could not create pixel buffer for: \(frameURL.path)")
	}
	let time = CMTime(value: frame.decodeTime, timescale: manifest.timeScale)
	guard adaptor.append(pixelBuffer, withPresentationTime: time) else {
		fail("failed to append frame at \(frame.decodeTime): \(writer.error?.localizedDescription ?? "unknown error")")
	}
}

input.markAsFinished()
let last = manifest.frames[manifest.frames.count - 1]
writer.endSession(atSourceTime: CMTime(value: last.decodeTime + last.duration, timescale: manifest.timeScale))

let semaphore = DispatchSemaphore(value: 0)
writer.finishWriting {
	semaphore.signal()
}
semaphore.wait()

guard writer.status == .completed else {
	fail("writer failed: \(writer.error?.localizedDescription ?? "unknown error")")
}

if let audioURL {
	mux(videoURL: videoOnlyURL, audioURL: audioURL, outputURL: outputURL)
	try? FileManager.default.removeItem(at: videoOnlyURL)
}

print(outputURL.path)
