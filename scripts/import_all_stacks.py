#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.12"
# ///
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import re
import shutil
import sqlite3
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_STACKS_DIR = Path("/Users/jrepp/d/pantechnicon/stacks")
ARCHIVE_SUFFIXES = (".sit", ".hqx")
IGNORED_INPUT_NAMES = {".mirror-manifest.tsv", ".mirror.log", ".DS_Store"}


@dataclass(frozen=True)
class ClassifiedFile:
    path: Path
    rel_path: str
    bytes: int
    extension: str
    finder_type: str
    binary_type: str
    binary_size: int | None
    decision: str
    reason: str


@dataclass(frozen=True)
class ImportResult:
    attempt_id: int
    status: str
    exit_code: int
    warnings: int
    errors: int
    status_lines: int
    blocks: int | None
    resources: int | None
    log_path: Path
    output_package: Path | None


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Extract HyperCard archives, run stackimport, and index import results in SQLite.",
    )
    parser.add_argument(
        "--stacks-dir",
        type=Path,
        default=Path(os.environ.get("STACKS_DIR", DEFAULT_STACKS_DIR)),
        help=f"Directory to scan. Default: {DEFAULT_STACKS_DIR}",
    )
    parser.add_argument(
        "--stackimport-bin",
        type=Path,
        default=Path(os.environ.get("STACKIMPORT_BIN", REPO_ROOT / "build" / "stackimport")),
        help="stackimport binary. Default: build/stackimport",
    )
    parser.add_argument(
        "--unar-bin",
        default=os.environ.get("UNAR_BIN", "unar"),
        help="unar binary for .sit/.hqx extraction. Default: unar",
    )
    parser.add_argument(
        "--run-root",
        type=Path,
        default=Path(os.environ.get("RUN_ROOT", REPO_ROOT / "import-runs")),
        help="Directory for captured runs. Default: import-runs",
    )
    parser.add_argument(
        "--run-id",
        default=os.environ.get("RUN_ID", datetime.now().strftime("%Y%m%d-%H%M%S")),
        help="Run directory name. Default: timestamp",
    )
    parser.add_argument(
        "--keep-going",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Continue after failed imports. Default: true",
    )
    args, stackimport_args = parser.parse_known_args()
    if stackimport_args and stackimport_args[0] == "--":
        stackimport_args = stackimport_args[1:]
    args.stackimport_args = stackimport_args
    return args


def connect_database(path: Path) -> sqlite3.Connection:
    conn = sqlite3.connect(path)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA journal_mode = WAL")
    conn.execute("PRAGMA foreign_keys = ON")
    conn.executescript(
        """
        CREATE TABLE IF NOT EXISTS runs (
            id TEXT PRIMARY KEY,
            started_at TEXT NOT NULL,
            finished_at TEXT,
            stacks_dir TEXT NOT NULL,
            stackimport_bin TEXT NOT NULL,
            unar_bin TEXT NOT NULL,
            run_dir TEXT NOT NULL,
            stackimport_args TEXT NOT NULL,
            processed INTEGER DEFAULT 0,
            stack_ok INTEGER DEFAULT 0,
            stack_failed INTEGER DEFAULT 0,
            no_stack_inputs INTEGER DEFAULT 0,
            extract_failed INTEGER DEFAULT 0
        );

        CREATE TABLE IF NOT EXISTS source_files (
            id INTEGER PRIMARY KEY,
            run_id TEXT NOT NULL REFERENCES runs(id) ON DELETE CASCADE,
            rel_path TEXT NOT NULL,
            path TEXT NOT NULL,
            bytes INTEGER NOT NULL,
            extension TEXT NOT NULL,
            is_archive INTEGER NOT NULL,
            status TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS extracted_files (
            id INTEGER PRIMARY KEY,
            source_id INTEGER NOT NULL REFERENCES source_files(id) ON DELETE CASCADE,
            rel_path TEXT NOT NULL,
            path TEXT NOT NULL,
            bytes INTEGER NOT NULL,
            extension TEXT NOT NULL,
            finder_type TEXT NOT NULL,
            binary_type TEXT NOT NULL,
            binary_size INTEGER,
            decision TEXT NOT NULL,
            reason TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS external_binary_files (
            id INTEGER PRIMARY KEY,
            run_id TEXT NOT NULL REFERENCES runs(id) ON DELETE CASCADE,
            source_id INTEGER NOT NULL REFERENCES source_files(id) ON DELETE CASCADE,
            extracted_file_id INTEGER REFERENCES extracted_files(id) ON DELETE CASCADE,
            archive_input TEXT NOT NULL,
            external_input TEXT NOT NULL,
            path TEXT NOT NULL,
            bytes INTEGER NOT NULL,
            extension TEXT NOT NULL,
            finder_type TEXT NOT NULL,
            binary_type TEXT NOT NULL,
            binary_size INTEGER,
            sha256 TEXT NOT NULL,
            external_kind TEXT NOT NULL,
            classification_reason TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS import_attempts (
            id INTEGER PRIMARY KEY,
            run_id TEXT NOT NULL REFERENCES runs(id) ON DELETE CASCADE,
            source_id INTEGER NOT NULL REFERENCES source_files(id) ON DELETE CASCADE,
            extracted_file_id INTEGER REFERENCES extracted_files(id) ON DELETE SET NULL,
            archive_input TEXT NOT NULL,
            import_input TEXT NOT NULL,
            import_path TEXT NOT NULL,
            status TEXT NOT NULL,
            exit_code INTEGER NOT NULL,
            warnings INTEGER NOT NULL,
            errors INTEGER NOT NULL,
            status_lines INTEGER NOT NULL,
            blocks INTEGER,
            resources INTEGER,
            log_path TEXT NOT NULL,
            output_package TEXT
        );

        CREATE TABLE IF NOT EXISTS stacks (
            id INTEGER PRIMARY KEY,
            run_id TEXT NOT NULL REFERENCES runs(id) ON DELETE CASCADE,
            attempt_id INTEGER NOT NULL REFERENCES import_attempts(id) ON DELETE CASCADE,
            source_id INTEGER NOT NULL REFERENCES source_files(id) ON DELETE CASCADE,
            extracted_file_id INTEGER REFERENCES extracted_files(id) ON DELETE SET NULL,
            archive_input TEXT NOT NULL,
            import_input TEXT NOT NULL,
            stack_name TEXT NOT NULL,
            stack_json_id INTEGER,
            card_count_declared INTEGER,
            card_width INTEGER,
            card_height INTEGER,
            project_user_level INTEGER,
            created_by_version TEXT,
            last_compacted_version TEXT,
            last_edited_version TEXT,
            first_edited_version TEXT,
            font_table_id INTEGER,
            style_table_id INTEGER,
            stack_script_bytes INTEGER NOT NULL,
            output_package TEXT NOT NULL,
            UNIQUE(attempt_id)
        );

        CREATE TABLE IF NOT EXISTS diagnostics (
            id INTEGER PRIMARY KEY,
            attempt_id INTEGER NOT NULL REFERENCES import_attempts(id) ON DELETE CASCADE,
            severity TEXT NOT NULL,
            line INTEGER NOT NULL,
            message TEXT NOT NULL,
            gap_kind TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS status_messages (
            id INTEGER PRIMARY KEY,
            attempt_id INTEGER NOT NULL REFERENCES import_attempts(id) ON DELETE CASCADE,
            line INTEGER NOT NULL,
            message TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS output_files (
            id INTEGER PRIMARY KEY,
            attempt_id INTEGER NOT NULL REFERENCES import_attempts(id) ON DELETE CASCADE,
            stack_id INTEGER REFERENCES stacks(id) ON DELETE CASCADE,
            rel_path TEXT NOT NULL,
            path TEXT NOT NULL,
            bytes INTEGER NOT NULL,
            extension TEXT NOT NULL,
            kind TEXT NOT NULL,
            sha256 TEXT NOT NULL,
            logical_kind TEXT NOT NULL,
            logical_id INTEGER,
            referenced_by TEXT
        );

        CREATE TABLE IF NOT EXISTS json_sections (
            id INTEGER PRIMARY KEY,
            attempt_id INTEGER NOT NULL REFERENCES import_attempts(id) ON DELETE CASCADE,
            stack_id INTEGER REFERENCES stacks(id) ON DELETE CASCADE,
            output_file_id INTEGER REFERENCES output_files(id) ON DELETE CASCADE,
            file_rel_path TEXT NOT NULL,
            depth INTEGER NOT NULL,
            tag TEXT NOT NULL,
            attrs_json TEXT NOT NULL,
            text_size INTEGER NOT NULL
        );

        CREATE TABLE IF NOT EXISTS parsed_objects (
            id INTEGER PRIMARY KEY,
            run_id TEXT NOT NULL REFERENCES runs(id) ON DELETE CASCADE,
            stack_id INTEGER NOT NULL REFERENCES stacks(id) ON DELETE CASCADE,
            attempt_id INTEGER NOT NULL REFERENCES import_attempts(id) ON DELETE CASCADE,
            output_file_id INTEGER REFERENCES output_files(id) ON DELETE CASCADE,
            object_kind TEXT NOT NULL,
            object_id INTEGER,
            name TEXT,
            file_ref TEXT,
            owner_id INTEGER,
            parent_kind TEXT,
            parent_id INTEGER,
            part_type TEXT,
            layer TEXT,
            text_bytes INTEGER NOT NULL,
            script_bytes INTEGER NOT NULL,
            attrs_json TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS parsed_parts (
            id INTEGER PRIMARY KEY,
            parsed_object_id INTEGER NOT NULL REFERENCES parsed_objects(id) ON DELETE CASCADE,
            stack_id INTEGER NOT NULL REFERENCES stacks(id) ON DELETE CASCADE,
            attempt_id INTEGER NOT NULL REFERENCES import_attempts(id) ON DELETE CASCADE,
            output_file_id INTEGER REFERENCES output_files(id) ON DELETE CASCADE,
            container_kind TEXT NOT NULL,
            container_id INTEGER,
            part_id INTEGER,
            part_type TEXT NOT NULL,
            name TEXT,
            style TEXT,
            visible TEXT,
            enabled TEXT,
            shared_text TEXT,
            lock_text TEXT,
            text_align TEXT,
            font TEXT,
            text_size INTEGER,
            text_style TEXT,
            rect_left INTEGER,
            rect_top INTEGER,
            rect_right INTEGER,
            rect_bottom INTEGER,
            script_bytes INTEGER NOT NULL,
            attrs_json TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS parsed_content (
            id INTEGER PRIMARY KEY,
            stack_id INTEGER NOT NULL REFERENCES stacks(id) ON DELETE CASCADE,
            attempt_id INTEGER NOT NULL REFERENCES import_attempts(id) ON DELETE CASCADE,
            output_file_id INTEGER REFERENCES output_files(id) ON DELETE CASCADE,
            container_kind TEXT NOT NULL,
            container_id INTEGER,
            layer TEXT NOT NULL,
            part_id INTEGER,
            text_bytes INTEGER NOT NULL,
            line_count INTEGER NOT NULL,
            sha256 TEXT NOT NULL,
            sample TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS media_refs (
            id INTEGER PRIMARY KEY,
            stack_id INTEGER NOT NULL REFERENCES stacks(id) ON DELETE CASCADE,
            attempt_id INTEGER NOT NULL REFERENCES import_attempts(id) ON DELETE CASCADE,
            media_id INTEGER,
            media_type TEXT NOT NULL,
            file_ref TEXT NOT NULL,
            output_file_id INTEGER REFERENCES output_files(id) ON DELETE SET NULL
        );

        CREATE TABLE IF NOT EXISTS binary_chunks (
            id INTEGER PRIMARY KEY,
            run_id TEXT NOT NULL REFERENCES runs(id) ON DELETE CASCADE,
            stack_id INTEGER REFERENCES stacks(id) ON DELETE CASCADE,
            attempt_id INTEGER NOT NULL REFERENCES import_attempts(id) ON DELETE CASCADE,
            chunk_type TEXT NOT NULL,
            chunk_id INTEGER,
            chunk_bytes INTEGER,
            status TEXT NOT NULL,
            understood INTEGER NOT NULL,
            evidence TEXT NOT NULL,
            log_line INTEGER,
            output_file_id INTEGER REFERENCES output_files(id) ON DELETE SET NULL
        );

        CREATE TABLE IF NOT EXISTS embedded_files (
            id INTEGER PRIMARY KEY,
            run_id TEXT NOT NULL REFERENCES runs(id) ON DELETE CASCADE,
            stack_id INTEGER NOT NULL REFERENCES stacks(id) ON DELETE CASCADE,
            attempt_id INTEGER NOT NULL REFERENCES import_attempts(id) ON DELETE CASCADE,
            output_file_id INTEGER REFERENCES output_files(id) ON DELETE SET NULL,
            rel_path TEXT NOT NULL,
            embedded_kind TEXT NOT NULL,
            logical_kind TEXT NOT NULL,
            logical_id INTEGER,
            bytes INTEGER NOT NULL,
            sha256 TEXT NOT NULL,
            referenced_by TEXT,
            source_chunk_type TEXT,
            source_chunk_id INTEGER
        );

        CREATE TABLE IF NOT EXISTS stack_statistics (
            id INTEGER PRIMARY KEY,
            stack_id INTEGER NOT NULL REFERENCES stacks(id) ON DELETE CASCADE,
            attempt_id INTEGER NOT NULL REFERENCES import_attempts(id) ON DELETE CASCADE,
            metric TEXT NOT NULL,
            value INTEGER NOT NULL,
            detail TEXT NOT NULL DEFAULT ''
        );

        CREATE TABLE IF NOT EXISTS format_gaps (
            id INTEGER PRIMARY KEY,
            run_id TEXT NOT NULL REFERENCES runs(id) ON DELETE CASCADE,
            source_id INTEGER REFERENCES source_files(id) ON DELETE CASCADE,
            extracted_file_id INTEGER REFERENCES extracted_files(id) ON DELETE CASCADE,
            attempt_id INTEGER REFERENCES import_attempts(id) ON DELETE CASCADE,
            kind TEXT NOT NULL,
            detail TEXT NOT NULL,
            subject TEXT NOT NULL,
            log_path TEXT
        );

        CREATE INDEX IF NOT EXISTS idx_source_files_run ON source_files(run_id);
        CREATE INDEX IF NOT EXISTS idx_extracted_files_source ON extracted_files(source_id);
        CREATE INDEX IF NOT EXISTS idx_external_binary_files_run ON external_binary_files(run_id);
        CREATE INDEX IF NOT EXISTS idx_external_binary_files_kind ON external_binary_files(external_kind);
        CREATE INDEX IF NOT EXISTS idx_external_binary_files_hash ON external_binary_files(sha256);
        CREATE INDEX IF NOT EXISTS idx_import_attempts_run ON import_attempts(run_id);
        CREATE INDEX IF NOT EXISTS idx_stacks_run ON stacks(run_id);
        CREATE INDEX IF NOT EXISTS idx_stacks_attempt ON stacks(attempt_id);
        CREATE INDEX IF NOT EXISTS idx_diagnostics_attempt ON diagnostics(attempt_id);
        CREATE INDEX IF NOT EXISTS idx_output_files_attempt ON output_files(attempt_id);
        CREATE INDEX IF NOT EXISTS idx_output_files_stack ON output_files(stack_id);
        CREATE INDEX IF NOT EXISTS idx_json_sections_tag ON json_sections(tag);
        CREATE INDEX IF NOT EXISTS idx_json_sections_stack ON json_sections(stack_id);
        CREATE INDEX IF NOT EXISTS idx_parsed_objects_stack_kind ON parsed_objects(stack_id, object_kind);
        CREATE INDEX IF NOT EXISTS idx_parsed_parts_stack_type ON parsed_parts(stack_id, part_type);
        CREATE INDEX IF NOT EXISTS idx_parsed_content_stack_part ON parsed_content(stack_id, part_id);
        CREATE INDEX IF NOT EXISTS idx_media_refs_stack ON media_refs(stack_id);
        CREATE INDEX IF NOT EXISTS idx_binary_chunks_stack_type ON binary_chunks(stack_id, chunk_type);
        CREATE INDEX IF NOT EXISTS idx_binary_chunks_attempt ON binary_chunks(attempt_id);
        CREATE INDEX IF NOT EXISTS idx_embedded_files_stack_kind ON embedded_files(stack_id, embedded_kind);
        CREATE INDEX IF NOT EXISTS idx_embedded_files_hash ON embedded_files(sha256);
        CREATE INDEX IF NOT EXISTS idx_stack_statistics_metric ON stack_statistics(metric);
        CREATE INDEX IF NOT EXISTS idx_format_gaps_kind ON format_gaps(kind);
        """
    )
    return conn


def text_or_empty(value: bytes) -> str:
    return value.decode("macroman", errors="replace").replace("\x00", "").strip()


def extension_for(path: Path) -> str:
    suffixes = path.suffixes
    if len(suffixes) >= 2 and "".join(suffixes[-2:]).lower() == ".sit.hqx":
        return "sit.hqx"
    return path.suffix[1:] if path.suffix else ""


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def log_stem_for(rel_path: str) -> str:
    safe = re.sub(r"[^A-Za-z0-9._-]+", "_", rel_path).strip("_") or "input"
    digest = hashlib.sha1(rel_path.encode("utf-8", errors="surrogateescape")).hexdigest()[:12]
    return f"{digest}-{safe}"


def finder_type_for(path: Path) -> str:
    getxattr = getattr(os, "getxattr", None)
    if getxattr is not None:
        try:
            finder_info = getxattr(path, "com.apple.FinderInfo")
        except OSError:
            finder_info = b""
    else:
        completed = subprocess.run(
            ["xattr", "-px", "com.apple.FinderInfo", str(path)],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            check=False,
        )
        if completed.returncode != 0:
            finder_info = b""
        else:
            finder_info = bytes.fromhex("".join(completed.stdout.split()))
    return text_or_empty(finder_info[:4])


def classify_file(path: Path, rel_path: str) -> ClassifiedFile:
    data = path.read_bytes()[:12]
    bytes_count = path.stat().st_size
    extension = extension_for(path)
    finder_type = finder_type_for(path)
    binary_size = int.from_bytes(data[:4], byteorder="big") if len(data) >= 4 else None
    binary_type = text_or_empty(data[4:8]) if len(data) >= 8 else ""

    lower_extension = extension.lower()
    decision = "skip"
    reason = "unrecognized"
    if finder_type == "STAK":
        decision = "stack"
        reason = "finder_type_stak"
    elif binary_type == "STAK" and binary_size is not None and 12 <= binary_size <= bytes_count:
        decision = "stack"
        reason = "binary_stak_header"
    elif lower_extension in {"stak", "stack", "stk"}:
        decision = "stack"
        reason = "stack_extension"
    elif binary_type == "STAK":
        reason = "binary_stak_header_size_mismatch"

    return ClassifiedFile(
        path=path,
        rel_path=rel_path,
        bytes=bytes_count,
        extension=extension,
        finder_type=finder_type,
        binary_type=binary_type,
        binary_size=binary_size,
        decision=decision,
        reason=reason,
    )


def external_kind_for(classified: ClassifiedFile) -> str:
    finder_type = classified.finder_type
    extension = classified.extension.lower()
    binary_type = classified.binary_type
    if finder_type in {"PICT"} or extension in {"pict", "pic"}:
        return "picture"
    if finder_type in {"TEXT"} or extension in {"txt", "text", "md", "c", "h", "html", "htm"}:
        return "text"
    if finder_type in {"APPL"}:
        return "application"
    if finder_type in {"MooV"} or extension in {"mov", "movie"}:
        return "movie"
    if finder_type in {"snd "} or extension in {"snd", "aif", "aiff", "wav"}:
        return "sound"
    if finder_type in {"Mcr$", "Mcr0"}:
        return "macro"
    if finder_type in {"ICON", "ICN#", "cicn", "CURS"}:
        return "icon_or_cursor"
    if binary_type in {"STAK"}:
        return "stack_like_unimported"
    if extension in {"sit", "hqx", "bin", "data", "rsrc", "dfont"}:
        return "binary_container"
    if extension in {"pdf"}:
        return "document"
    return "unknown_binary"


def insert_external_binary(
    conn: sqlite3.Connection,
    run_id: str,
    source_id: int,
    extracted_file_id: int | None,
    archive_input: str,
    external_input: str,
    classified: ClassifiedFile,
) -> None:
    conn.execute(
        """
        INSERT INTO external_binary_files(
            run_id, source_id, extracted_file_id, archive_input, external_input, path,
            bytes, extension, finder_type, binary_type, binary_size, sha256,
            external_kind, classification_reason
        )
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            run_id,
            source_id,
            extracted_file_id,
            archive_input,
            external_input,
            str(classified.path),
            classified.bytes,
            classified.extension,
            classified.finder_type,
            classified.binary_type,
            classified.binary_size,
            file_sha256(classified.path),
            external_kind_for(classified),
            classified.reason,
        ),
    )


def iter_input_files(stacks_dir: Path) -> list[Path]:
    files: list[Path] = []
    for path in stacks_dir.rglob("*"):
        if not path.is_file():
            continue
        if any(part.endswith(".xstk") for part in path.parts):
            continue
        if path.name in IGNORED_INPUT_NAMES or path.name.endswith(".part"):
            continue
        files.append(path)
    return sorted(files, key=lambda item: str(item.relative_to(stacks_dir)))


def iter_extracted_files(extract_dir: Path) -> list[Path]:
    files: list[Path] = []
    for path in extract_dir.rglob("*"):
        if not path.is_file():
            continue
        if any(part.endswith(".xstk") for part in path.parts):
            continue
        if path.name in IGNORED_INPUT_NAMES:
            continue
        files.append(path)
    return sorted(files, key=lambda item: str(item.relative_to(extract_dir)))


def is_archive(path: Path) -> bool:
    lower = path.name.lower()
    return lower.endswith(ARCHIVE_SUFFIXES)


def count_matching(lines: list[str], pattern: str) -> int:
    return sum(1 for line in lines if pattern in line)


def first_status_number(lines: list[str], regex: re.Pattern[str]) -> int | None:
    for line in lines:
        match = regex.search(line)
        if match:
            return int(match.group(1))
    return None


def gap_kind_for(message: str) -> str:
    if "Skipping block" in message:
        return "block"
    if "Skipping code resource" in message or re.search(r"Unknown type .* resource", message):
        return "resource"
    if "Premature end" in message or "Couldn't parse" in message:
        return "chunk"
    if "Couldn't find stack block" in message:
        return "file"
    if "Assertion failed" in message or "Abort trap" in message or "Segmentation fault" in message:
        return "crash"
    return "diagnostic"


def chunk_type_for_log_type(log_type: str) -> str:
    return log_type.strip().upper()


def output_logical_to_chunk_type(logical_kind: str) -> str:
    return {
        "background": "BKGD",
        "card": "CARD",
        "bmap": "BMAP",
        "master": "MAST",
        "pagesetup": "PRST",
        "printsettings": "PRNT",
        "reporttemplate": "PRFT",
        "stack": "STAK",
    }.get(logical_kind, logical_kind.upper())


def chunk_status_from_log(line: str) -> tuple[str, str, int | None, int | None] | None:
    processing = re.search(r"Status: Processing '([^']+)' #(-?\d+)(?:\s+[0-9A-Fa-f]+)? \((\d+) bytes\)", line)
    if processing:
        return "parsed", chunk_type_for_log_type(processing.group(1)), int(processing.group(2)), int(processing.group(3))
    skipped_status = re.search(r"Status: Skipping '([^']+)' #(-?\d+) \((\d+) bytes\)", line)
    if skipped_status:
        return "skipped", chunk_type_for_log_type(skipped_status.group(1)), int(skipped_status.group(2)), int(skipped_status.group(3))
    skipped_warning = re.search(r"Warning: Skipping block\s+([A-Za-z0-9 ]{4}) #(-?\d+)", line)
    if skipped_warning:
        return "unhandled", chunk_type_for_log_type(skipped_warning.group(1)), int(skipped_warning.group(2)), None
    return None


def output_package_for(import_path: Path) -> Path:
    path_text = str(import_path)
    stak_pos = path_text.rfind(".stak")
    if stak_pos != -1:
        return Path(f"{path_text[:stak_pos]}.xstk")
    return Path(f"{path_text}.xstk")


def kind_for_output(path: Path) -> str:
    suffix = path.suffix.lower()
    if suffix == ".json":
        return "json"
    if suffix in {".pbm", ".png", ".jpg", ".jpeg", ".gif"}:
        return "image"
    if suffix in {".aiff", ".aif", ".wav", ".snd"}:
        return "sound"
    if suffix in {".raw", ".data"}:
        return "raw"
    return "other"


def logical_file_info(rel_path: str) -> tuple[str, int | None]:
    name = Path(rel_path).name
    if name == "printsettings.json":
        return "printsettings", None
    match = re.match(r"^(card|background|BMAP|PAT|stylesheet|master|pagesetup|reporttemplate|printsettings)_(-?\d+)", name)
    if match:
        logical_kind = match.group(1).lower()
        logical_id = int(match.group(2))
        return logical_kind, logical_id
    if name == "project.json":
        return "project", None
    if name == "stack_-1.json":
        return "stack", -1
    return kind_for_output(Path(rel_path)), None


def optional_int(value: str | None) -> int | None:
    if value is None:
        return None
    value = value.strip()
    if not value:
        return None
    try:
        return int(value)
    except ValueError:
        return None


def insert_source(conn: sqlite3.Connection, run_id: str, path: Path, rel_path: str) -> int:
    cursor = conn.execute(
        """
        INSERT INTO source_files(run_id, rel_path, path, bytes, extension, is_archive, status)
        VALUES (?, ?, ?, ?, ?, ?, ?)
        """,
        (run_id, rel_path, str(path), path.stat().st_size, extension_for(path), int(is_archive(path)), "pending"),
    )
    return int(cursor.lastrowid)


def update_source_status(conn: sqlite3.Connection, source_id: int, status: str) -> None:
    conn.execute("UPDATE source_files SET status = ? WHERE id = ?", (status, source_id))


def insert_extracted(conn: sqlite3.Connection, source_id: int, classified: ClassifiedFile) -> int:
    cursor = conn.execute(
        """
        INSERT INTO extracted_files(
            source_id, rel_path, path, bytes, extension, finder_type, binary_type,
            binary_size, decision, reason
        )
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            source_id,
            classified.rel_path,
            str(classified.path),
            classified.bytes,
            classified.extension,
            classified.finder_type,
            classified.binary_type,
            classified.binary_size,
            classified.decision,
            classified.reason,
        ),
    )
    return int(cursor.lastrowid)


def insert_format_gap(
    conn: sqlite3.Connection,
    run_id: str,
    kind: str,
    detail: str,
    subject: str,
    *,
    source_id: int | None = None,
    extracted_file_id: int | None = None,
    attempt_id: int | None = None,
    log_path: Path | None = None,
) -> None:
    conn.execute(
        """
        INSERT INTO format_gaps(run_id, source_id, extracted_file_id, attempt_id, kind, detail, subject, log_path)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (run_id, source_id, extracted_file_id, attempt_id, kind, detail, subject, str(log_path) if log_path else None),
    )


def run_stackimport(
    conn: sqlite3.Connection,
    run_id: str,
    source_id: int,
    extracted_file_id: int | None,
    archive_input: str,
    import_path: Path,
    display_input: str,
    log_path: Path,
    stackimport_bin: Path,
    stackimport_args: list[str],
) -> ImportResult:
    command = [str(stackimport_bin), *stackimport_args, str(import_path)]
    completed = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)

    log_path.write_text(
        "\n".join(
            [
                f"Original input: {archive_input}",
                f"Import input: {import_path}",
                f"Display input: {display_input}",
                f"Command: {subprocess.list2cmdline(command)}",
                "",
                completed.stdout,
            ]
        ),
        encoding="utf-8",
        errors="replace",
    )
    lines = log_path.read_text(encoding="utf-8", errors="replace").splitlines()
    warnings = count_matching(lines, "Warning:")
    errors = count_matching(lines, "Error:")
    status_lines = count_matching(lines, "Status:")
    blocks = first_status_number(lines, re.compile(r"Status: Found ([0-9]+) blocks in file\."))
    resources = first_status_number(lines, re.compile(r"Status: Found ([0-9]+) resources in file\."))
    status = "ok" if completed.returncode == 0 else "failed"
    output_package = output_package_for(import_path)
    output_package_text = str(output_package) if output_package.exists() else None

    cursor = conn.execute(
        """
        INSERT INTO import_attempts(
            run_id, source_id, extracted_file_id, archive_input, import_input, import_path, status,
            exit_code, warnings, errors, status_lines, blocks, resources, log_path, output_package
        )
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            run_id,
            source_id,
            extracted_file_id,
            archive_input,
            display_input,
            str(import_path),
            status,
            completed.returncode,
            warnings,
            errors,
            status_lines,
            blocks,
            resources,
            str(log_path),
            output_package_text,
        ),
    )
    attempt_id = int(cursor.lastrowid)

    for line_number, line in enumerate(lines, start=1):
        if "Status:" in line:
            conn.execute(
                "INSERT INTO status_messages(attempt_id, line, message) VALUES (?, ?, ?)",
                (attempt_id, line_number, line),
            )
        chunk_status = chunk_status_from_log(line)
        if chunk_status is not None:
            chunk_state, chunk_type, chunk_id, chunk_bytes = chunk_status
            understood = chunk_state == "parsed" or chunk_type == "FREE"
            conn.execute(
                """
                INSERT INTO binary_chunks(
                    run_id, attempt_id, chunk_type, chunk_id, chunk_bytes, status,
                    understood, evidence, log_line
                )
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    run_id,
                    attempt_id,
                    chunk_type,
                    chunk_id,
                    chunk_bytes,
                    chunk_state,
                    int(understood),
                    line,
                    line_number,
                ),
            )
        severity = ""
        if "Warning:" in line:
            severity = "warning"
        elif "Error:" in line or "Assertion failed" in line or "Abort trap" in line or "Segmentation fault" in line:
            severity = "error"
        if severity:
            gap_kind = gap_kind_for(line)
            conn.execute(
                """
                INSERT INTO diagnostics(attempt_id, severity, line, message, gap_kind)
                VALUES (?, ?, ?, ?, ?)
                """,
                (attempt_id, severity, line_number, line, gap_kind),
            )
            insert_format_gap(
                conn,
                run_id,
                gap_kind,
                line,
                display_input,
                source_id=source_id,
                extracted_file_id=extracted_file_id,
                attempt_id=attempt_id,
                log_path=log_path,
            )

    if status == "failed":
        detail = f"exit_code={completed.returncode}; warnings={warnings}; errors={errors}; blocks={blocks}; resources={resources}"
        insert_format_gap(
            conn,
            run_id,
            "import_failure",
            detail,
            display_input,
            source_id=source_id,
            extracted_file_id=extracted_file_id,
            attempt_id=attempt_id,
            log_path=log_path,
        )

    if output_package.exists():
        index_output_package(conn, attempt_id, output_package)

    return ImportResult(
        attempt_id=attempt_id,
        status=status,
        exit_code=completed.returncode,
        warnings=warnings,
        errors=errors,
        status_lines=status_lines,
        blocks=blocks,
        resources=resources,
        log_path=log_path,
        output_package=output_package if output_package.exists() else None,
    )


def parse_json_if_possible(path: Path) -> object | None:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None


def json_int(obj: dict[str, object] | None, key: str) -> int | None:
    if not obj:
        return None
    value = obj.get(key)
    return value if isinstance(value, int) else None


def json_text(obj: dict[str, object] | None, key: str, default: str = "") -> str:
    if not obj:
        return default
    value = obj.get(key)
    return value if isinstance(value, str) else default


def json_bool_text(obj: dict[str, object], key: str) -> str:
    value = obj.get(key)
    if isinstance(value, bool):
        return "true" if value else "false"
    return ""


def json_int_value(obj: dict[str, object], key: str) -> int | None:
    value = obj.get(key)
    return value if isinstance(value, int) else None


def text_sha256(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8", errors="replace")).hexdigest()


def insert_stack_record(conn: sqlite3.Connection, attempt_id: int, output_package: Path) -> int:
    attempt = conn.execute(
        "SELECT run_id, source_id, extracted_file_id, archive_input, import_input FROM import_attempts WHERE id = ?",
        (attempt_id,),
    ).fetchone()
    project_json = parse_json_if_possible(output_package / "project.json")
    stack_json = parse_json_if_possible(output_package / "stack_-1.json")
    project_root = project_json if isinstance(project_json, dict) else None
    stack_root = stack_json if isinstance(stack_json, dict) else None
    stack_name = json_text(stack_root, "name", Path(output_package).stem)

    cursor = conn.execute(
        """
        INSERT INTO stacks(
            run_id, attempt_id, source_id, extracted_file_id, archive_input, import_input,
            stack_name, stack_json_id, card_count_declared, card_width, card_height,
            project_user_level, created_by_version, last_compacted_version,
            last_edited_version, first_edited_version, font_table_id, style_table_id,
            stack_script_bytes, output_package
        )
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            attempt["run_id"],
            attempt_id,
            attempt["source_id"],
            attempt["extracted_file_id"],
            attempt["archive_input"],
            attempt["import_input"],
            stack_name,
            json_int(stack_root, "id"),
            json_int(stack_root, "cardCount"),
            json_int(stack_root, "cardWidth"),
            json_int(stack_root, "cardHeight"),
            json_int(project_root, "userLevel"),
            json_text(project_root, "createdByVersion"),
            json_text(project_root, "lastCompactedVersion"),
            json_text(project_root, "lastEditedVersion"),
            json_text(project_root, "firstEditedVersion"),
            json_int(stack_root, "fontTableBlockId"),
            json_int(stack_root, "styleTableBlockId"),
            len(json_text(stack_root, "script")),
            str(output_package),
        ),
    )
    stack_id = int(cursor.lastrowid)
    insert_stack_summary_objects(conn, stack_id, attempt_id, project_root, stack_root)
    return stack_id


def insert_parsed_object(
    conn: sqlite3.Connection,
    *,
    run_id: str,
    stack_id: int,
    attempt_id: int,
    output_file_id: int | None,
    object_kind: str,
    object_id: int | None = None,
    name: str | None = None,
    file_ref: str | None = None,
    owner_id: int | None = None,
    parent_kind: str | None = None,
    parent_id: int | None = None,
    part_type: str | None = None,
    layer: str | None = None,
    text_bytes: int = 0,
    script_bytes: int = 0,
    attrs: dict[str, object] | None = None,
) -> int:
    cursor = conn.execute(
        """
        INSERT INTO parsed_objects(
            run_id, stack_id, attempt_id, output_file_id, object_kind, object_id, name,
            file_ref, owner_id, parent_kind, parent_id, part_type, layer, text_bytes,
            script_bytes, attrs_json
        )
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            run_id,
            stack_id,
            attempt_id,
            output_file_id,
            object_kind,
            object_id,
            name,
            file_ref,
            owner_id,
            parent_kind,
            parent_id,
            part_type,
            layer,
            text_bytes,
            script_bytes,
            json.dumps(attrs or {}, sort_keys=True),
        ),
    )
    return int(cursor.lastrowid)


def insert_parsed_part(
    conn: sqlite3.Connection,
    *,
    parsed_object_id: int,
    stack_id: int,
    attempt_id: int,
    output_file_id: int,
    container_kind: str,
    container_id: int | None,
    part: dict[str, object],
) -> None:
    rect = part.get("rect") if isinstance(part.get("rect"), dict) else {}
    text_styles = part.get("textStyles")
    text_style = ",".join(str(item) for item in text_styles) if isinstance(text_styles, list) else ""
    conn.execute(
        """
        INSERT INTO parsed_parts(
            parsed_object_id, stack_id, attempt_id, output_file_id, container_kind, container_id,
            part_id, part_type, name, style, visible, enabled, shared_text, lock_text,
            text_align, font, text_size, text_style, rect_left, rect_top, rect_right,
            rect_bottom, script_bytes, attrs_json
        )
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            parsed_object_id,
            stack_id,
            attempt_id,
            output_file_id,
            container_kind,
            container_id,
            json_int_value(part, "id"),
            json_text(part, "type", "unknown"),
            part.get("name") if isinstance(part.get("name"), str) else None,
            part.get("style") if isinstance(part.get("style"), str) else None,
            json_bool_text(part, "visible"),
            json_bool_text(part, "enabled"),
            json_bool_text(part, "sharedText"),
            json_bool_text(part, "lockText"),
            part.get("textAlign") if isinstance(part.get("textAlign"), str) else None,
            part.get("font") if isinstance(part.get("font"), str) else None,
            json_int_value(part, "textSize"),
            text_style,
            json_int_value(rect, "left") if isinstance(rect, dict) else None,
            json_int_value(rect, "top") if isinstance(rect, dict) else None,
            json_int_value(rect, "right") if isinstance(rect, dict) else None,
            json_int_value(rect, "bottom") if isinstance(rect, dict) else None,
            len(json_text(part, "script")),
            json.dumps(part, sort_keys=True),
        ),
    )


def insert_parsed_content(
    conn: sqlite3.Connection,
    *,
    stack_id: int,
    attempt_id: int,
    output_file_id: int,
    container_kind: str,
    container_id: int | None,
    content: dict[str, object],
) -> None:
    text = json_text(content, "text")
    conn.execute(
        """
        INSERT INTO parsed_content(
            stack_id, attempt_id, output_file_id, container_kind, container_id, layer,
            part_id, text_bytes, line_count, sha256, sample
        )
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            stack_id,
            attempt_id,
            output_file_id,
            container_kind,
            container_id,
            json_text(content, "layer", container_kind),
            json_int_value(content, "id"),
            len(text),
            len(text.splitlines()) if text else 0,
            text_sha256(text),
            text[:240],
        ),
    )


def insert_stack_summary_objects(
    conn: sqlite3.Connection,
    stack_id: int,
    attempt_id: int,
    project_root: dict[str, object] | None,
    stack_root: dict[str, object] | None,
) -> None:
    run_id = conn.execute("SELECT run_id FROM import_attempts WHERE id = ?", (attempt_id,)).fetchone()["run_id"]
    insert_parsed_object(
        conn,
        run_id=run_id,
        stack_id=stack_id,
        attempt_id=attempt_id,
        output_file_id=None,
        object_kind="stack",
        object_id=json_int(stack_root, "id"),
        name=json_text(stack_root, "name"),
        text_bytes=0,
        script_bytes=0,
    )
    if not project_root:
        return
    # Media references are populated by typed JSON once those resources are lifted
    # out of the legacy payload wrappers.


def referenced_by_for(conn: sqlite3.Connection, stack_id: int, rel_path: str) -> str:
    row = conn.execute(
        "SELECT object_kind, object_id FROM parsed_objects WHERE stack_id = ? AND file_ref = ? ORDER BY id LIMIT 1",
        (stack_id, rel_path),
    ).fetchone()
    if row:
        object_id = "" if row["object_id"] is None else str(row["object_id"])
        return f"{row['object_kind']}:{object_id}"
    row = conn.execute(
        "SELECT media_type, media_id FROM media_refs WHERE stack_id = ? AND file_ref = ? ORDER BY id LIMIT 1",
        (stack_id, rel_path),
    ).fetchone()
    if row:
        media_id = "" if row["media_id"] is None else str(row["media_id"])
        return f"media:{row['media_type']}:{media_id}"
    return ""


def index_output_package(conn: sqlite3.Connection, attempt_id: int, output_package: Path) -> None:
    stack_id = insert_stack_record(conn, attempt_id, output_package)
    run_id = conn.execute("SELECT run_id FROM import_attempts WHERE id = ?", (attempt_id,)).fetchone()["run_id"]
    for output_file in sorted((path for path in output_package.rglob("*") if path.is_file()), key=lambda item: str(item)):
        rel_path = str(output_file.relative_to(output_package))
        logical_kind, logical_id = logical_file_info(rel_path)
        referenced_by = referenced_by_for(conn, stack_id, rel_path)
        cursor = conn.execute(
            """
            INSERT INTO output_files(
                attempt_id, stack_id, rel_path, path, bytes, extension, kind, sha256,
                logical_kind, logical_id, referenced_by
            )
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                attempt_id,
                stack_id,
                rel_path,
                str(output_file),
                output_file.stat().st_size,
                extension_for(output_file),
                kind_for_output(output_file),
                file_sha256(output_file),
                logical_kind,
                logical_id,
                referenced_by,
            ),
        )
        output_file_id = int(cursor.lastrowid)
        if output_file.suffix.lower() == ".json":
            index_json_sections(conn, attempt_id, stack_id, output_file_id, rel_path, output_file)
            index_parsed_json_objects(conn, attempt_id, stack_id, output_file_id, rel_path, output_file)
    link_media_refs_to_output_files(conn, stack_id)
    link_output_file_references(conn, stack_id)
    link_binary_chunks_to_stack_and_outputs(conn, run_id, stack_id, attempt_id)
    index_embedded_files(conn, run_id, stack_id, attempt_id)
    insert_stack_statistics(conn, stack_id, attempt_id)


def index_json_sections(
    conn: sqlite3.Connection,
    attempt_id: int,
    stack_id: int,
    output_file_id: int,
    rel_path: str,
    output_file: Path,
) -> None:
    payload = parse_json_if_possible(output_file)
    if payload is None:
        insert_format_gap(
            conn,
            conn.execute("SELECT run_id FROM import_attempts WHERE id = ?", (attempt_id,)).fetchone()["run_id"],
            "generated_json_parse",
            "could not parse generated JSON",
            rel_path,
            attempt_id=attempt_id,
            log_path=output_file,
        )
        return

    def visit(value: object, depth: int, key: str) -> None:
        attrs = {}
        text_size = 0
        if isinstance(value, dict):
            attrs = {"keys": sorted(value.keys())}
        elif isinstance(value, list):
            attrs = {"items": len(value)}
        elif value is not None:
            text_size = len(str(value))
        conn.execute(
            """
            INSERT INTO json_sections(attempt_id, stack_id, output_file_id, file_rel_path, depth, tag, attrs_json, text_size)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                attempt_id,
                stack_id,
                output_file_id,
                rel_path,
                depth,
                key,
                json.dumps(attrs, sort_keys=True),
                text_size,
            ),
        )
        if isinstance(value, dict):
            for child_key, child_value in value.items():
                visit(child_value, depth + 1, child_key)
        elif isinstance(value, list):
            for index, child_value in enumerate(value):
                visit(child_value, depth + 1, f"[{index}]")

    visit(payload, 0, "$")


def index_parsed_json_objects(
    conn: sqlite3.Connection,
    attempt_id: int,
    stack_id: int,
    output_file_id: int,
    rel_path: str,
    output_file: Path,
) -> None:
    payload = parse_json_if_possible(output_file)
    if not isinstance(payload, dict):
        return
    run_id = conn.execute("SELECT run_id FROM import_attempts WHERE id = ?", (attempt_id,)).fetchone()["run_id"]
    logical_kind, logical_id = logical_file_info(rel_path)
    if logical_kind in {"card", "background", "master"}:
        script = payload.get("script", "")
        content_text_bytes = 0
        contents = payload.get("contents")
        if isinstance(contents, list):
            for content in contents:
                if isinstance(content, dict) and isinstance(content.get("text"), str):
                    content_text_bytes += len(content["text"])
        container_object_id = insert_parsed_object(
            conn,
            run_id=run_id,
            stack_id=stack_id,
            attempt_id=attempt_id,
            output_file_id=output_file_id,
            object_kind=logical_kind,
            object_id=logical_id,
            name=payload.get("name") if isinstance(payload.get("name"), str) else None,
            file_ref=rel_path,
            text_bytes=content_text_bytes,
            script_bytes=len(script) if isinstance(script, str) else 0,
            attrs={"format": payload.get("format"), "blockType": payload.get("blockType")},
        )
        if isinstance(contents, list):
            for content in contents:
                if isinstance(content, dict):
                    insert_parsed_content(
                        conn,
                        stack_id=stack_id,
                        attempt_id=attempt_id,
                        output_file_id=output_file_id,
                        container_kind=logical_kind,
                        container_id=logical_id,
                        content=content,
                    )
        parts = payload.get("parts")
        if isinstance(parts, list):
            for part in parts:
                if not isinstance(part, dict):
                    continue
                parsed_object_id = insert_parsed_object(
                    conn,
                    run_id=run_id,
                    stack_id=stack_id,
                    attempt_id=attempt_id,
                    output_file_id=output_file_id,
                    object_kind="part",
                    object_id=part.get("id") if isinstance(part.get("id"), int) else None,
                    name=part.get("name") if isinstance(part.get("name"), str) else None,
                    parent_kind=logical_kind,
                    parent_id=logical_id,
                    part_type=part.get("type") if isinstance(part.get("type"), str) else None,
                    layer=logical_kind,
                    script_bytes=len(part.get("script")) if isinstance(part.get("script"), str) else 0,
                    attrs=part,
                )
                insert_parsed_part(
                    conn,
                    parsed_object_id=parsed_object_id,
                    stack_id=stack_id,
                    attempt_id=attempt_id,
                    output_file_id=output_file_id,
                    container_kind=logical_kind,
                    container_id=logical_id,
                    part=part,
                )
        if logical_kind == "master":
            # Master references are still retained in attrs/json_sections; there
            # are no layer parts or contents to lift into typed tables.
            _ = container_object_id
    elif logical_kind == "project" and isinstance(payload.get("blocks"), list):
        for block in payload["blocks"]:
            if not isinstance(block, dict):
                continue
            insert_parsed_object(
                conn,
                run_id=run_id,
                stack_id=stack_id,
                attempt_id=attempt_id,
                output_file_id=output_file_id,
                object_kind="binary_chunk",
                object_id=block.get("id") if isinstance(block.get("id"), int) else None,
                part_type=block.get("type") if isinstance(block.get("type"), str) else None,
                attrs=block,
            )
        outputs = payload.get("outputs")
        if isinstance(outputs, list):
            for output in outputs:
                if not isinstance(output, dict):
                    continue
                file_ref = output.get("file")
                kind = output.get("kind")
                if not isinstance(file_ref, str) or not isinstance(kind, str):
                    continue
                conn.execute(
                    """
                    INSERT INTO media_refs(stack_id, attempt_id, media_id, media_type, file_ref)
                    VALUES (?, ?, ?, ?, ?)
                    """,
                    (
                        stack_id,
                        attempt_id,
                        output.get("id") if isinstance(output.get("id"), int) else None,
                        kind,
                        file_ref,
                    ),
                )
    elif logical_kind == "stack":
        for key in ("listBlockId", "fontTableBlockId", "styleTableBlockId"):
            value = payload.get(key)
            if not isinstance(value, int):
                continue
            insert_parsed_object(
                conn,
                run_id=run_id,
                stack_id=stack_id,
                attempt_id=attempt_id,
                output_file_id=output_file_id,
                object_kind=key,
                object_id=value,
                parent_kind="stack",
                parent_id=json_int(payload, "id"),
            )
        layers = payload.get("layers")
        if isinstance(layers, list):
            for layer in layers:
                if not isinstance(layer, dict):
                    continue
                kind = layer.get("kind")
                insert_parsed_object(
                    conn,
                    run_id=run_id,
                    stack_id=stack_id,
                    attempt_id=attempt_id,
                    output_file_id=output_file_id,
                    object_kind=f"{kind}_ref" if isinstance(kind, str) else "layer_ref",
                    object_id=layer.get("id") if isinstance(layer.get("id"), int) else None,
                    name=layer.get("name") if isinstance(layer.get("name"), str) else None,
                    file_ref=layer.get("file") if isinstance(layer.get("file"), str) else None,
                    owner_id=layer.get("owner") if isinstance(layer.get("owner"), int) else None,
                    attrs=layer,
                )


def link_media_refs_to_output_files(conn: sqlite3.Connection, stack_id: int) -> None:
    conn.execute(
        """
        UPDATE media_refs
        SET output_file_id = (
            SELECT output_files.id
            FROM output_files
            WHERE output_files.stack_id = media_refs.stack_id
              AND output_files.rel_path = media_refs.file_ref
            LIMIT 1
        )
        WHERE stack_id = ?
        """,
        (stack_id,),
    )


def link_output_file_references(conn: sqlite3.Connection, stack_id: int) -> None:
    conn.execute(
        """
        UPDATE output_files
        SET referenced_by = COALESCE(
            (
                SELECT parsed_objects.object_kind || ':' || COALESCE(parsed_objects.object_id, '')
                FROM parsed_objects
                WHERE parsed_objects.stack_id = output_files.stack_id
                  AND parsed_objects.file_ref = output_files.rel_path
                ORDER BY parsed_objects.id
                LIMIT 1
            ),
            (
                SELECT 'media:' || media_refs.media_type || ':' || COALESCE(media_refs.media_id, '')
                FROM media_refs
                WHERE media_refs.stack_id = output_files.stack_id
                  AND media_refs.file_ref = output_files.rel_path
                ORDER BY media_refs.id
                LIMIT 1
            ),
            referenced_by
        )
        WHERE stack_id = ?
        """,
        (stack_id,),
    )


def link_binary_chunks_to_stack_and_outputs(
    conn: sqlite3.Connection,
    run_id: str,
    stack_id: int,
    attempt_id: int,
) -> None:
    conn.execute("UPDATE binary_chunks SET stack_id = ? WHERE attempt_id = ?", (stack_id, attempt_id))
    conn.execute(
        """
        UPDATE binary_chunks
        SET output_file_id = (
            SELECT output_files.id
            FROM output_files
            WHERE output_files.stack_id = binary_chunks.stack_id
              AND output_files.logical_id = binary_chunks.chunk_id
              AND (
                  (binary_chunks.chunk_type = 'CARD' AND output_files.logical_kind = 'card')
                  OR (binary_chunks.chunk_type = 'BKGD' AND output_files.logical_kind = 'background')
                  OR (binary_chunks.chunk_type = 'BMAP' AND output_files.logical_kind = 'bmap')
                  OR (binary_chunks.chunk_type = 'MAST' AND output_files.logical_kind = 'master')
                  OR (binary_chunks.chunk_type = 'PRST' AND output_files.logical_kind = 'pagesetup')
                  OR (binary_chunks.chunk_type = 'PRFT' AND output_files.logical_kind = 'reporttemplate')
              )
            LIMIT 1
        )
        WHERE attempt_id = ?
        """,
        (attempt_id,),
    )
    existing = {
        (row["chunk_type"], row["chunk_id"])
        for row in conn.execute(
            "SELECT chunk_type, chunk_id FROM binary_chunks WHERE attempt_id = ?",
            (attempt_id,),
        )
    }
    for row in conn.execute(
        """
        SELECT id, logical_kind, logical_id, bytes, rel_path
        FROM output_files
        WHERE stack_id = ?
          AND logical_kind IN ('card', 'background', 'bmap', 'master', 'pagesetup', 'printsettings', 'reporttemplate', 'stack')
        """,
        (stack_id,),
    ):
        chunk_type = output_logical_to_chunk_type(row["logical_kind"])
        chunk_id = row["logical_id"]
        if (chunk_type, chunk_id) in existing:
            continue
        conn.execute(
            """
            INSERT INTO binary_chunks(
                run_id, stack_id, attempt_id, chunk_type, chunk_id, chunk_bytes,
                status, understood, evidence, output_file_id
            )
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                run_id,
                stack_id,
                attempt_id,
                chunk_type,
                chunk_id,
                row["bytes"],
                "derived_from_output",
                1,
                f"generated output {row['rel_path']}",
                row["id"],
            ),
        )


def embedded_kind_for_output(row: sqlite3.Row) -> str:
    if row["kind"] == "image":
        return "image"
    if row["kind"] == "sound":
        return "sound"
    if row["kind"] == "raw":
        return "raw_binary"
    if row["logical_kind"] in {"pat", "bmap"}:
        return "bitmap"
    if row["logical_kind"] in {"externalcommand", "externalfunction"}:
        return "code_resource"
    return "embedded_file"


def index_embedded_files(conn: sqlite3.Connection, run_id: str, stack_id: int, attempt_id: int) -> None:
    for row in conn.execute(
        """
        SELECT id, rel_path, bytes, kind, logical_kind, logical_id, sha256, referenced_by
        FROM output_files
        WHERE stack_id = ?
          AND (
              kind IN ('image', 'sound', 'raw')
              OR logical_kind IN ('pat', 'bmap')
          )
        """,
        (stack_id,),
    ):
        source_chunk_type = output_logical_to_chunk_type(row["logical_kind"]) if row["logical_id"] is not None else None
        conn.execute(
            """
            INSERT INTO embedded_files(
                run_id, stack_id, attempt_id, output_file_id, rel_path, embedded_kind,
                logical_kind, logical_id, bytes, sha256, referenced_by,
                source_chunk_type, source_chunk_id
            )
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                run_id,
                stack_id,
                attempt_id,
                row["id"],
                row["rel_path"],
                embedded_kind_for_output(row),
                row["logical_kind"],
                row["logical_id"],
                row["bytes"],
                row["sha256"],
                row["referenced_by"],
                source_chunk_type,
                row["logical_id"],
            ),
        )


def insert_stat(conn: sqlite3.Connection, stack_id: int, attempt_id: int, metric: str, value: int, detail: str = "") -> None:
    conn.execute(
        "INSERT INTO stack_statistics(stack_id, attempt_id, metric, value, detail) VALUES (?, ?, ?, ?, ?)",
        (stack_id, attempt_id, metric, value, detail),
    )


def insert_stack_statistics(conn: sqlite3.Connection, stack_id: int, attempt_id: int) -> None:
    scalar_queries = {
        "output_file_count": "SELECT COUNT(*) FROM output_files WHERE stack_id = ?",
        "output_file_bytes": "SELECT COALESCE(SUM(bytes), 0) FROM output_files WHERE stack_id = ?",
        "json_file_count": "SELECT COUNT(*) FROM output_files WHERE stack_id = ? AND kind = 'json'",
        "image_file_count": "SELECT COUNT(*) FROM output_files WHERE stack_id = ? AND kind = 'image'",
        "raw_file_count": "SELECT COUNT(*) FROM output_files WHERE stack_id = ? AND kind = 'raw'",
        "json_section_count": "SELECT COUNT(*) FROM json_sections WHERE stack_id = ?",
        "parsed_object_count": "SELECT COUNT(*) FROM parsed_objects WHERE stack_id = ?",
        "part_count": "SELECT COUNT(*) FROM parsed_parts WHERE stack_id = ?",
        "content_count": "SELECT COUNT(*) FROM parsed_content WHERE stack_id = ?",
        "content_bytes": "SELECT COALESCE(SUM(text_bytes), 0) FROM parsed_content WHERE stack_id = ?",
        "part_script_bytes": "SELECT COALESCE(SUM(script_bytes), 0) FROM parsed_parts WHERE stack_id = ?",
        "media_ref_count": "SELECT COUNT(*) FROM media_refs WHERE stack_id = ?",
        "binary_chunk_count": "SELECT COUNT(*) FROM binary_chunks WHERE stack_id = ?",
        "understood_chunk_count": "SELECT COUNT(*) FROM binary_chunks WHERE stack_id = ? AND understood = 1",
        "not_understood_chunk_count": "SELECT COUNT(*) FROM binary_chunks WHERE stack_id = ? AND understood = 0",
        "embedded_file_count": "SELECT COUNT(*) FROM embedded_files WHERE stack_id = ?",
        "embedded_file_bytes": "SELECT COALESCE(SUM(bytes), 0) FROM embedded_files WHERE stack_id = ?",
    }
    for metric, query in scalar_queries.items():
        value = conn.execute(query, (stack_id,)).fetchone()[0]
        insert_stat(conn, stack_id, attempt_id, metric, int(value or 0))
    grouped_queries = {
        "output_file_kind": "SELECT kind, COUNT(*) FROM output_files WHERE stack_id = ? GROUP BY kind",
        "logical_file_kind": "SELECT logical_kind, COUNT(*) FROM output_files WHERE stack_id = ? GROUP BY logical_kind",
        "object_kind": "SELECT object_kind, COUNT(*) FROM parsed_objects WHERE stack_id = ? GROUP BY object_kind",
        "part_type": "SELECT part_type, COUNT(*) FROM parsed_parts WHERE stack_id = ? GROUP BY part_type",
        "json_tag": "SELECT tag, COUNT(*) FROM json_sections WHERE stack_id = ? GROUP BY tag",
        "binary_chunk_type": "SELECT chunk_type, COUNT(*) FROM binary_chunks WHERE stack_id = ? GROUP BY chunk_type",
        "binary_chunk_status": "SELECT status, COUNT(*) FROM binary_chunks WHERE stack_id = ? GROUP BY status",
        "embedded_file_kind": "SELECT embedded_kind, COUNT(*) FROM embedded_files WHERE stack_id = ? GROUP BY embedded_kind",
    }
    for metric, query in grouped_queries.items():
        for detail, value in conn.execute(query, (stack_id,)):
            insert_stat(conn, stack_id, attempt_id, metric, int(value or 0), str(detail or ""))


def write_tsv(path: Path, rows: list[dict[str, object]], fieldnames: list[str]) -> None:
    with path.open("w", newline="", encoding="utf-8") as file:
        writer = csv.DictWriter(file, fieldnames=fieldnames, dialect="excel-tab", extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def export_reports(conn: sqlite3.Connection, run_dir: Path, run_id: str) -> None:
    report_queries = {
        "summary.tsv": (
            """
            SELECT a.status, a.exit_code, a.warnings, a.errors, a.status_lines, a.blocks, a.resources,
                   s.stack_name, s.card_count_declared, s.card_width, s.card_height,
                   a.archive_input, a.import_input, a.log_path, a.output_package
            FROM import_attempts a
            LEFT JOIN stacks s ON s.attempt_id = a.id
            WHERE a.run_id = ?
            ORDER BY a.id
            """,
            [
                "status",
                "exit_code",
                "warnings",
                "errors",
                "status_lines",
                "blocks",
                "resources",
                "stack_name",
                "card_count_declared",
                "card_width",
                "card_height",
                "archive_input",
                "import_input",
                "log_path",
                "output_package",
            ],
        ),
        "diagnostics.tsv": (
            """
            SELECT d.severity, a.import_input AS input, d.line, d.gap_kind, d.message, a.log_path
            FROM diagnostics d
            JOIN import_attempts a ON a.id = d.attempt_id
            WHERE a.run_id = ?
            ORDER BY d.severity, d.gap_kind, d.message
            """,
            ["severity", "input", "line", "gap_kind", "message", "log_path"],
        ),
        "classifications.tsv": (
            """
            SELECT s.rel_path AS archive_input, e.rel_path AS extracted_input, e.bytes, e.extension,
                   e.finder_type, e.binary_type, e.binary_size, e.decision, e.reason
            FROM extracted_files e
            JOIN source_files s ON s.id = e.source_id
            WHERE s.run_id = ?
            ORDER BY s.rel_path, e.rel_path
            """,
            [
                "archive_input",
                "extracted_input",
                "bytes",
                "extension",
                "finder_type",
                "binary_type",
                "binary_size",
                "decision",
                "reason",
            ],
        ),
        "format-gaps.tsv": (
            """
            SELECT kind, detail, subject, log_path
            FROM format_gaps
            WHERE run_id = ?
            ORDER BY kind, detail, subject
            """,
            ["kind", "detail", "subject", "log_path"],
        ),
        "output-files.tsv": (
            """
            SELECT s.stack_name, a.import_input, f.rel_path, f.bytes, f.extension,
                   f.kind, f.logical_kind, f.logical_id, f.referenced_by, f.sha256
            FROM output_files f
            JOIN import_attempts a ON a.id = f.attempt_id
            LEFT JOIN stacks s ON s.id = f.stack_id
            WHERE a.run_id = ?
            ORDER BY s.stack_name, a.import_input, f.rel_path
            """,
            [
                "stack_name",
                "import_input",
                "rel_path",
                "bytes",
                "extension",
                "kind",
                "logical_kind",
                "logical_id",
                "referenced_by",
                "sha256",
            ],
        ),
        "json-sections.tsv": (
            """
            SELECT s.stack_name, a.import_input, x.file_rel_path, x.depth, x.tag, x.attrs_json, x.text_size
            FROM json_sections x
            JOIN import_attempts a ON a.id = x.attempt_id
            LEFT JOIN stacks s ON s.id = x.stack_id
            WHERE a.run_id = ?
            ORDER BY s.stack_name, a.import_input, x.file_rel_path, x.id
            """,
            ["stack_name", "import_input", "file_rel_path", "depth", "tag", "attrs_json", "text_size"],
        ),
        "stacks.tsv": (
            """
            SELECT stack_name, import_input, card_count_declared, card_width, card_height,
                   project_user_level, created_by_version, last_compacted_version,
                   last_edited_version, first_edited_version, font_table_id, style_table_id,
                   stack_script_bytes, output_package
            FROM stacks
            WHERE run_id = ?
            ORDER BY stack_name, import_input
            """,
            [
                "stack_name",
                "import_input",
                "card_count_declared",
                "card_width",
                "card_height",
                "project_user_level",
                "created_by_version",
                "last_compacted_version",
                "last_edited_version",
                "first_edited_version",
                "font_table_id",
                "style_table_id",
                "stack_script_bytes",
                "output_package",
            ],
        ),
        "stack-statistics.tsv": (
            """
            SELECT s.stack_name, s.import_input, st.metric, st.detail, st.value
            FROM stack_statistics st
            JOIN stacks s ON s.id = st.stack_id
            WHERE s.run_id = ?
            ORDER BY s.stack_name, st.metric, st.detail
            """,
            ["stack_name", "import_input", "metric", "detail", "value"],
        ),
        "parsed-parts.tsv": (
            """
            SELECT s.stack_name, s.import_input, of.rel_path AS file_rel_path,
                   p.container_kind, p.container_id, p.part_id, p.part_type, p.name,
                   p.style, p.visible, p.enabled, p.shared_text, p.lock_text,
                   p.text_align, p.font, p.text_size, p.text_style,
                   p.rect_left, p.rect_top, p.rect_right, p.rect_bottom,
                   p.script_bytes
            FROM parsed_parts p
            JOIN stacks s ON s.id = p.stack_id
            LEFT JOIN output_files of ON of.id = p.output_file_id
            WHERE s.run_id = ?
            ORDER BY s.stack_name, of.rel_path, p.part_id
            """,
            [
                "stack_name",
                "import_input",
                "file_rel_path",
                "container_kind",
                "container_id",
                "part_id",
                "part_type",
                "name",
                "style",
                "visible",
                "enabled",
                "shared_text",
                "lock_text",
                "text_align",
                "font",
                "text_size",
                "text_style",
                "rect_left",
                "rect_top",
                "rect_right",
                "rect_bottom",
                "script_bytes",
            ],
        ),
        "parsed-content.tsv": (
            """
            SELECT s.stack_name, s.import_input, of.rel_path AS file_rel_path,
                   c.container_kind, c.container_id, c.layer, c.part_id,
                   c.text_bytes, c.line_count, c.sha256, c.sample
            FROM parsed_content c
            JOIN stacks s ON s.id = c.stack_id
            LEFT JOIN output_files of ON of.id = c.output_file_id
            WHERE s.run_id = ?
            ORDER BY s.stack_name, of.rel_path, c.part_id
            """,
            [
                "stack_name",
                "import_input",
                "file_rel_path",
                "container_kind",
                "container_id",
                "layer",
                "part_id",
                "text_bytes",
                "line_count",
                "sha256",
                "sample",
            ],
        ),
        "binary-chunks.tsv": (
            """
            SELECT s.stack_name, a.import_input, c.chunk_type, c.chunk_id, c.chunk_bytes,
                   c.status, c.understood, of.rel_path AS output_file, c.evidence, c.log_line
            FROM binary_chunks c
            JOIN import_attempts a ON a.id = c.attempt_id
            LEFT JOIN stacks s ON s.id = c.stack_id
            LEFT JOIN output_files of ON of.id = c.output_file_id
            WHERE a.run_id = ?
            ORDER BY s.stack_name, c.chunk_type, c.chunk_id, c.id
            """,
            [
                "stack_name",
                "import_input",
                "chunk_type",
                "chunk_id",
                "chunk_bytes",
                "status",
                "understood",
                "output_file",
                "evidence",
                "log_line",
            ],
        ),
        "embedded-files.tsv": (
            """
            SELECT s.stack_name, s.import_input, e.rel_path, e.embedded_kind,
                   e.logical_kind, e.logical_id, e.bytes, e.sha256, e.referenced_by,
                   e.source_chunk_type, e.source_chunk_id
            FROM embedded_files e
            JOIN stacks s ON s.id = e.stack_id
            WHERE s.run_id = ?
            ORDER BY s.stack_name, e.embedded_kind, e.rel_path
            """,
            [
                "stack_name",
                "import_input",
                "rel_path",
                "embedded_kind",
                "logical_kind",
                "logical_id",
                "bytes",
                "sha256",
                "referenced_by",
                "source_chunk_type",
                "source_chunk_id",
            ],
        ),
        "chunk-usage.tsv": (
            """
            SELECT c.chunk_type, c.status, c.understood, COUNT(*) AS stack_count,
                   GROUP_CONCAT(DISTINCT s.stack_name) AS stacks
            FROM binary_chunks c
            JOIN stacks s ON s.id = c.stack_id
            WHERE s.run_id = ?
            GROUP BY c.chunk_type, c.status, c.understood
            ORDER BY c.understood, c.chunk_type, c.status
            """,
            ["chunk_type", "status", "understood", "stack_count", "stacks"],
        ),
        "embedded-file-usage.tsv": (
            """
            SELECT e.embedded_kind, e.logical_kind, e.sha256, e.bytes, COUNT(*) AS occurrence_count,
                   GROUP_CONCAT(DISTINCT s.stack_name) AS stacks
            FROM embedded_files e
            JOIN stacks s ON s.id = e.stack_id
            WHERE s.run_id = ?
            GROUP BY e.embedded_kind, e.logical_kind, e.sha256, e.bytes
            ORDER BY occurrence_count DESC, e.embedded_kind, e.logical_kind
            """,
            ["embedded_kind", "logical_kind", "sha256", "bytes", "occurrence_count", "stacks"],
        ),
        "external-binary-files.tsv": (
            """
            SELECT archive_input, external_input, external_kind, bytes, extension,
                   finder_type, binary_type, binary_size, sha256, classification_reason, path
            FROM external_binary_files
            WHERE run_id = ?
            ORDER BY archive_input, external_kind, external_input
            """,
            [
                "archive_input",
                "external_input",
                "external_kind",
                "bytes",
                "extension",
                "finder_type",
                "binary_type",
                "binary_size",
                "sha256",
                "classification_reason",
                "path",
            ],
        ),
        "external-binary-usage.tsv": (
            """
            SELECT external_kind, extension, finder_type, binary_type, COUNT(*) AS occurrence_count,
                   COUNT(DISTINCT sha256) AS distinct_hashes,
                   SUM(bytes) AS total_bytes
            FROM external_binary_files
            WHERE run_id = ?
            GROUP BY external_kind, extension, finder_type, binary_type
            ORDER BY occurrence_count DESC, total_bytes DESC
            """,
            [
                "external_kind",
                "extension",
                "finder_type",
                "binary_type",
                "occurrence_count",
                "distinct_hashes",
                "total_bytes",
            ],
        ),
    }
    for filename, (query, fieldnames) in report_queries.items():
        rows = [dict(row) for row in conn.execute(query, (run_id,))]
        write_tsv(run_dir / filename, rows, fieldnames)

    counts = [dict(row) for row in conn.execute(
        """
        SELECT kind, COUNT(*) AS count
        FROM format_gaps
        WHERE run_id = ?
        GROUP BY kind
        ORDER BY kind
        """,
        (run_id,),
    )]
    write_tsv(run_dir / "format-gap-counts.tsv", counts, ["kind", "count"])


def main() -> int:
    args = parse_args()
    stacks_dir = args.stacks_dir.resolve()
    stackimport_bin = args.stackimport_bin.resolve()
    run_dir = (args.run_root / args.run_id).resolve()
    log_dir = run_dir / "logs"
    extract_dir = run_dir / "extracted"
    db_path = run_dir / "run.db"

    if not stacks_dir.is_dir():
        print(f"Error: stack directory not found: {stacks_dir}", file=sys.stderr)
        return 2
    if not stackimport_bin.is_file() or not os.access(stackimport_bin, os.X_OK):
        print(f"Error: stackimport binary is not executable: {stackimport_bin}", file=sys.stderr)
        print("Build it with: make stackimport", file=sys.stderr)
        return 3
    unar_path = shutil.which(args.unar_bin)
    if not unar_path:
        print(f"Error: unar binary not found: {args.unar_bin}", file=sys.stderr)
        return 4

    log_dir.mkdir(parents=True, exist_ok=True)
    extract_dir.mkdir(parents=True, exist_ok=True)
    conn = connect_database(db_path)
    started_at = datetime.now().isoformat(timespec="seconds")
    conn.execute(
        """
        INSERT OR REPLACE INTO runs(id, started_at, stacks_dir, stackimport_bin, unar_bin, run_dir, stackimport_args)
        VALUES (?, ?, ?, ?, ?, ?, ?)
        """,
        (args.run_id, started_at, str(stacks_dir), str(stackimport_bin), unar_path, str(run_dir), json.dumps(args.stackimport_args)),
    )
    conn.commit()

    processed = stack_ok = stack_failed = no_stack = extract_failed = 0
    for source_path in iter_input_files(stacks_dir):
        processed += 1
        rel_path = str(source_path.relative_to(stacks_dir))
        print(f"[{processed}] {rel_path}", flush=True)
        source_id = insert_source(conn, args.run_id, source_path, rel_path)
        log_stem = log_stem_for(rel_path)

        if is_archive(source_path):
            archive_extract_dir = extract_dir / log_stem
            archive_extract_dir.mkdir(parents=True, exist_ok=True)
            extract_log = log_dir / f"{log_stem}.unar.log"
            command = [unar_path, "-force-overwrite", "-output-directory", str(archive_extract_dir), str(source_path)]
            completed = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
            extract_log.write_text(completed.stdout, encoding="utf-8", errors="replace")
            if completed.returncode != 0:
                extract_failed += 1
                update_source_status(conn, source_id, "extract_failed")
                insert_format_gap(
                    conn,
                    args.run_id,
                    "extract_failure",
                    f"exit_code={completed.returncode}",
                    rel_path,
                    source_id=source_id,
                    log_path=extract_log,
                )
                conn.commit()
                continue

            import_count = 0
            for extracted_path in iter_extracted_files(archive_extract_dir):
                extracted_rel = str(extracted_path.relative_to(archive_extract_dir))
                classified = classify_file(extracted_path, extracted_rel)
                extracted_id = insert_extracted(conn, source_id, classified)
                display_input = f"{rel_path} -> {extracted_rel}"
                if classified.decision != "stack":
                    insert_external_binary(
                        conn,
                        args.run_id,
                        source_id,
                        extracted_id,
                        rel_path,
                        display_input,
                        classified,
                    )
                    insert_format_gap(
                        conn,
                        args.run_id,
                        "file",
                        f"reason={classified.reason}; finder_type={classified.finder_type}; binary_type={classified.binary_type}; extension={classified.extension}",
                        display_input,
                        source_id=source_id,
                        extracted_file_id=extracted_id,
                    )
                    continue
                import_count += 1
                result = run_stackimport(
                    conn,
                    args.run_id,
                    source_id,
                    extracted_id,
                    rel_path,
                    extracted_path,
                    display_input,
                    log_dir / f"{log_stem}.{import_count}.log",
                    stackimport_bin,
                    args.stackimport_args,
                )
                if result.status == "ok":
                    stack_ok += 1
                else:
                    stack_failed += 1
                    if not args.keep_going:
                        conn.commit()
                        return 1

            if import_count == 0:
                no_stack += 1
                update_source_status(conn, source_id, "no_stack")
            else:
                update_source_status(conn, source_id, "imported")
        else:
            classified = classify_file(source_path, rel_path)
            extracted_id = insert_extracted(conn, source_id, classified)
            if classified.decision != "stack":
                no_stack += 1
                update_source_status(conn, source_id, "no_stack")
                insert_external_binary(
                    conn,
                    args.run_id,
                    source_id,
                    extracted_id,
                    rel_path,
                    rel_path,
                    classified,
                )
                insert_format_gap(
                    conn,
                    args.run_id,
                    "file",
                    f"reason={classified.reason}; finder_type={classified.finder_type}; binary_type={classified.binary_type}; extension={classified.extension}",
                    rel_path,
                    source_id=source_id,
                    extracted_file_id=extracted_id,
                )
            else:
                result = run_stackimport(
                    conn,
                    args.run_id,
                    source_id,
                    extracted_id,
                    rel_path,
                    source_path,
                    rel_path,
                    log_dir / f"{log_stem}.log",
                    stackimport_bin,
                    args.stackimport_args,
                )
                update_source_status(conn, source_id, "imported")
                if result.status == "ok":
                    stack_ok += 1
                else:
                    stack_failed += 1
                    if not args.keep_going:
                        conn.commit()
                        return 1
        if processed % 10 == 0:
            conn.commit()

    conn.execute(
        """
        UPDATE runs
        SET finished_at = ?, processed = ?, stack_ok = ?, stack_failed = ?,
            no_stack_inputs = ?, extract_failed = ?
        WHERE id = ?
        """,
        (
            datetime.now().isoformat(timespec="seconds"),
            processed,
            stack_ok,
            stack_failed,
            no_stack,
            extract_failed,
            args.run_id,
        ),
    )
    conn.commit()
    export_reports(conn, run_dir, args.run_id)
    conn.close()

    print()
    print(f"Run directory: {run_dir}")
    print(f"Database: {db_path}")
    print(f"Summary: {run_dir / 'summary.tsv'}")
    print(f"Format gaps: {run_dir / 'format-gaps.tsv'}")
    print(f"Format gap counts: {run_dir / 'format-gap-counts.tsv'}")
    print(f"Output files: {run_dir / 'output-files.tsv'}")
    print(f"JSON sections: {run_dir / 'json-sections.tsv'}")
    print(f"Stacks: {run_dir / 'stacks.tsv'}")
    print(f"Stack statistics: {run_dir / 'stack-statistics.tsv'}")
    print(f"Parsed parts: {run_dir / 'parsed-parts.tsv'}")
    print(f"Parsed content: {run_dir / 'parsed-content.tsv'}")
    print(f"Binary chunks: {run_dir / 'binary-chunks.tsv'}")
    print(f"Chunk usage: {run_dir / 'chunk-usage.tsv'}")
    print(f"Embedded files: {run_dir / 'embedded-files.tsv'}")
    print(f"Embedded file usage: {run_dir / 'embedded-file-usage.tsv'}")
    print(f"External binary files: {run_dir / 'external-binary-files.tsv'}")
    print(f"External binary usage: {run_dir / 'external-binary-usage.tsv'}")
    print(
        f"Processed: {processed}, stack ok: {stack_ok}, stack failed: {stack_failed}, "
        f"no-stack inputs: {no_stack}, extract failed: {extract_failed}"
    )
    return 1 if stack_failed or extract_failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
