#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.12"
# ///
from __future__ import annotations

import argparse
import hashlib
import json
import os
import sqlite3
import struct
import sys
import zlib
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_RUN_ROOT = REPO_ROOT / "import-runs"
CONVERTER_NAME = "stackimport-embedded-converter"
CONVERTER_VERSION = "0.1"


@dataclass(frozen=True)
class PbmImage:
    width: int
    height: int
    row_bytes: int
    data: bytes


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert embedded stackimport outputs into modern inspection files with provenance.",
    )
    parser.add_argument(
        "--run-db",
        type=Path,
        help="Path to an import_all_stacks.py run.db. Defaults to the newest run under import-runs/.",
    )
    parser.add_argument(
        "--run-root",
        type=Path,
        default=Path(os.environ.get("RUN_ROOT", DEFAULT_RUN_ROOT)),
        help=f"Directory containing import runs. Default: {DEFAULT_RUN_ROOT}",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        help="Directory for converted outputs. Defaults to <run-dir>/embedded-conversions.",
    )
    parser.add_argument(
        "--limit",
        type=int,
        help="Maximum number of embedded file rows to process.",
    )
    parser.add_argument(
        "--embedded-kind",
        action="append",
        help="Filter by embedded_kind. May be repeated.",
    )
    parser.add_argument(
        "--logical-kind",
        action="append",
        help="Filter by logical_kind. May be repeated.",
    )
    parser.add_argument(
        "--no-db",
        action="store_true",
        help="Do not create/update embedded_conversions in the run database.",
    )
    return parser.parse_args()


def latest_run_db(run_root: Path) -> Path:
    candidates = sorted(
        (path for path in run_root.glob("*/run.db") if path.is_file()),
        key=lambda path: path.stat().st_mtime,
        reverse=True,
    )
    if not candidates:
        raise FileNotFoundError(f"No run.db files found under {run_root}")
    return candidates[0]


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_token(data: bytes, offset: int) -> tuple[bytes, int]:
    while offset < len(data):
        ch = data[offset]
        if ch in b" \t\r\n":
            offset += 1
            continue
        if ch == ord("#"):
            while offset < len(data) and data[offset] not in b"\r\n":
                offset += 1
            continue
        break
    start = offset
    while offset < len(data) and data[offset] not in b" \t\r\n":
        offset += 1
    if start == offset:
        raise ValueError("missing PBM token")
    return data[start:offset], offset


def parse_pbm(path: Path) -> PbmImage:
    data = path.read_bytes()
    magic, offset = read_token(data, 0)
    if magic != b"P4":
        raise ValueError(f"unsupported PBM magic {magic!r}")
    width_token, offset = read_token(data, offset)
    height_token, offset = read_token(data, offset)
    width = int(width_token)
    height = int(height_token)
    if width <= 0 or height <= 0:
        raise ValueError(f"invalid PBM dimensions {width}x{height}")
    if offset < len(data) and data[offset] in b" \t\r\n":
        offset += 1
    row_bytes = (width + 7) // 8
    expected = row_bytes * height
    payload = data[offset : offset + expected]
    if len(payload) != expected:
        raise ValueError(f"short PBM payload: expected {expected} bytes, got {len(payload)}")
    return PbmImage(width=width, height=height, row_bytes=row_bytes, data=payload)


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    crc = zlib.crc32(kind)
    crc = zlib.crc32(payload, crc)
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", crc & 0xFFFFFFFF)


def write_pbm_as_png(source: Path, target: Path) -> dict[str, int | str]:
    image = parse_pbm(source)
    rows = []
    for row in range(image.height):
        start = row * image.row_bytes
        rows.append(b"\x00" + image.data[start : start + image.row_bytes])
    ihdr = struct.pack(">IIBBBBB", image.width, image.height, 1, 0, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", ihdr) + png_chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + png_chunk(b"IEND", b"")
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(png)
    return {
        "width": image.width,
        "height": image.height,
        "bit_depth": 1,
        "color_type": "grayscale",
        "source_format": "PBM/P4",
        "target_format": "PNG",
    }


def connect_database(path: Path) -> sqlite3.Connection:
    conn = sqlite3.connect(path)
    conn.row_factory = sqlite3.Row
    return conn


def ensure_conversion_table(conn: sqlite3.Connection) -> None:
    conn.executescript(
        """
        CREATE TABLE IF NOT EXISTS embedded_conversions (
            id INTEGER PRIMARY KEY,
            embedded_file_id INTEGER NOT NULL REFERENCES embedded_files(id) ON DELETE CASCADE,
            stack_id INTEGER NOT NULL REFERENCES stacks(id) ON DELETE CASCADE,
            attempt_id INTEGER NOT NULL REFERENCES import_attempts(id) ON DELETE CASCADE,
            source_path TEXT NOT NULL,
            source_sha256 TEXT NOT NULL,
            source_bytes INTEGER NOT NULL,
            source_format TEXT NOT NULL,
            target_path TEXT,
            target_format TEXT NOT NULL,
            target_sha256 TEXT,
            target_bytes INTEGER,
            status TEXT NOT NULL,
            converter_name TEXT NOT NULL,
            converter_version TEXT NOT NULL,
            converted_at TEXT NOT NULL,
            provenance_json TEXT NOT NULL,
            message TEXT NOT NULL,
            UNIQUE(embedded_file_id, target_format)
        );
        CREATE INDEX IF NOT EXISTS idx_embedded_conversions_status ON embedded_conversions(status);
        CREATE INDEX IF NOT EXISTS idx_embedded_conversions_stack ON embedded_conversions(stack_id);
        """
    )


def query_embedded_files(conn: sqlite3.Connection, args: argparse.Namespace) -> list[sqlite3.Row]:
    clauses = []
    params: list[object] = []
    if args.embedded_kind:
        placeholders = ",".join("?" for _ in args.embedded_kind)
        clauses.append(f"e.embedded_kind IN ({placeholders})")
        params.extend(args.embedded_kind)
    if args.logical_kind:
        placeholders = ",".join("?" for _ in args.logical_kind)
        clauses.append(f"e.logical_kind IN ({placeholders})")
        params.extend(args.logical_kind)
    where = " AND ".join(clauses)
    if where:
        where = "WHERE " + where
    limit = ""
    if args.limit is not None:
        limit = "LIMIT ?"
        params.append(args.limit)
    return list(
        conn.execute(
            f"""
            SELECT
                e.id AS embedded_file_id,
                e.run_id,
                e.stack_id,
                e.attempt_id,
                e.output_file_id,
                e.rel_path,
                e.embedded_kind,
                e.logical_kind,
                e.logical_id,
                e.bytes,
                e.sha256,
                e.referenced_by,
                e.source_chunk_type,
                e.source_chunk_id,
                s.stack_name,
                s.archive_input,
                s.import_input,
                s.output_package,
                o.path AS source_path
            FROM embedded_files e
            JOIN stacks s ON s.id = e.stack_id
            LEFT JOIN output_files o ON o.id = e.output_file_id
            {where}
            ORDER BY s.stack_name, e.embedded_kind, e.logical_kind, e.rel_path
            {limit}
            """,
            params,
        )
    )


def safe_name(value: str) -> str:
    cleaned = []
    for ch in value:
        if ch.isalnum() or ch in {"-", "_", "."}:
            cleaned.append(ch)
        else:
            cleaned.append("_")
    return "".join(cleaned).strip("_") or "embedded"


def target_path_for(output_dir: Path, row: sqlite3.Row, target_extension: str) -> Path:
    stack_part = f"stack-{row['stack_id']}_{safe_name(row['stack_name'])}"
    stem = safe_name(Path(row["rel_path"]).with_suffix("").as_posix())
    name = f"embedded-{row['embedded_file_id']}_{stem}{target_extension}"
    return output_dir / stack_part / name


def provenance_for(
    *,
    row: sqlite3.Row,
    run_db: Path,
    source_path: Path,
    source_sha256: str,
    target_path: Path | None,
    target_sha256: str | None,
    converted_at: str,
    status: str,
    message: str,
    conversion_meta: dict[str, object],
) -> dict[str, object]:
    return {
        "converter": {
            "name": CONVERTER_NAME,
            "version": CONVERTER_VERSION,
            "converted_at": converted_at,
            "repository": str(REPO_ROOT),
            "run_db": str(run_db),
        },
        "source": {
            "embedded_file_id": row["embedded_file_id"],
            "run_id": row["run_id"],
            "stack_id": row["stack_id"],
            "stack_name": row["stack_name"],
            "archive_input": row["archive_input"],
            "import_input": row["import_input"],
            "output_package": row["output_package"],
            "rel_path": row["rel_path"],
            "path": str(source_path),
            "bytes": row["bytes"],
            "sha256": source_sha256,
            "embedded_kind": row["embedded_kind"],
            "logical_kind": row["logical_kind"],
            "logical_id": row["logical_id"],
            "referenced_by": row["referenced_by"],
            "source_chunk_type": row["source_chunk_type"],
            "source_chunk_id": row["source_chunk_id"],
        },
        "target": {
            "path": None if target_path is None else str(target_path),
            "sha256": target_sha256,
            "status": status,
            "message": message,
            **conversion_meta,
        },
    }


def write_sidecar(path: Path, provenance: dict[str, object]) -> None:
    sidecar = path.with_suffix(path.suffix + ".provenance.json")
    sidecar.write_text(json.dumps(provenance, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def upsert_conversion(
    conn: sqlite3.Connection,
    row: sqlite3.Row,
    source_path: Path,
    source_sha256: str,
    target_path: Path | None,
    target_format: str,
    target_sha256: str | None,
    status: str,
    converted_at: str,
    provenance: dict[str, object],
    message: str,
) -> None:
    target_bytes = target_path.stat().st_size if target_path and target_path.exists() else None
    conn.execute(
        """
        INSERT INTO embedded_conversions(
            embedded_file_id, stack_id, attempt_id, source_path, source_sha256, source_bytes,
            source_format, target_path, target_format, target_sha256, target_bytes, status,
            converter_name, converter_version, converted_at, provenance_json, message
        )
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(embedded_file_id, target_format) DO UPDATE SET
            source_path = excluded.source_path,
            source_sha256 = excluded.source_sha256,
            source_bytes = excluded.source_bytes,
            source_format = excluded.source_format,
            target_path = excluded.target_path,
            target_sha256 = excluded.target_sha256,
            target_bytes = excluded.target_bytes,
            status = excluded.status,
            converter_name = excluded.converter_name,
            converter_version = excluded.converter_version,
            converted_at = excluded.converted_at,
            provenance_json = excluded.provenance_json,
            message = excluded.message
        """,
        (
            row["embedded_file_id"],
            row["stack_id"],
            row["attempt_id"],
            str(source_path),
            source_sha256,
            row["bytes"],
            Path(row["rel_path"]).suffix.lower().lstrip(".") or row["logical_kind"],
            None if target_path is None else str(target_path),
            target_format,
            target_sha256,
            target_bytes,
            status,
            CONVERTER_NAME,
            CONVERTER_VERSION,
            converted_at,
            json.dumps(provenance, sort_keys=True),
            message,
        ),
    )


def convert_row(row: sqlite3.Row, run_db: Path, output_dir: Path) -> dict[str, object]:
    source_path_text = row["source_path"]
    converted_at = datetime.now(timezone.utc).isoformat()
    if not source_path_text:
        message = "embedded row has no linked output file"
        provenance = provenance_for(
            row=row,
            run_db=run_db,
            source_path=Path(""),
            source_sha256=row["sha256"],
            target_path=None,
            target_sha256=None,
            converted_at=converted_at,
            status="missing_source",
            message=message,
            conversion_meta={"target_format": "none"},
        )
        return {
            "row": row,
            "status": "missing_source",
            "target_format": "none",
            "target_path": None,
            "target_sha256": None,
            "provenance": provenance,
            "message": message,
        }

    source_path = Path(source_path_text)
    if not source_path.exists():
        message = "source file does not exist"
        provenance = provenance_for(
            row=row,
            run_db=run_db,
            source_path=source_path,
            source_sha256=row["sha256"],
            target_path=None,
            target_sha256=None,
            converted_at=converted_at,
            status="missing_source",
            message=message,
            conversion_meta={"target_format": "none"},
        )
        return {
            "row": row,
            "status": "missing_source",
            "target_format": "none",
            "target_path": None,
            "target_sha256": None,
            "provenance": provenance,
            "message": message,
        }

    source_sha256 = file_sha256(source_path)
    suffix = source_path.suffix.lower()
    if suffix == ".pbm":
        target_path = target_path_for(output_dir, row, ".png")
        try:
            conversion_meta = write_pbm_as_png(source_path, target_path)
            target_sha256 = file_sha256(target_path)
            status = "converted"
            message = "converted PBM/P4 to PNG"
            provenance = provenance_for(
                row=row,
                run_db=run_db,
                source_path=source_path,
                source_sha256=source_sha256,
                target_path=target_path,
                target_sha256=target_sha256,
                converted_at=converted_at,
                status=status,
                message=message,
                conversion_meta=conversion_meta,
            )
            write_sidecar(target_path, provenance)
            return {
                "row": row,
                "status": status,
                "target_format": "png",
                "target_path": target_path,
                "target_sha256": target_sha256,
                "provenance": provenance,
                "message": message,
            }
        except Exception as exc:  # noqa: BLE001 - batch conversion must continue.
            message = f"PBM conversion failed: {exc}"
            provenance = provenance_for(
                row=row,
                run_db=run_db,
                source_path=source_path,
                source_sha256=source_sha256,
                target_path=target_path,
                target_sha256=None,
                converted_at=converted_at,
                status="failed",
                message=message,
                conversion_meta={"target_format": "png"},
            )
            return {
                "row": row,
                "status": "failed",
                "target_format": "png",
                "target_path": target_path,
                "target_sha256": None,
                "provenance": provenance,
                "message": message,
            }

    message = f"no converter registered for extension {suffix or '<none>'}"
    provenance = provenance_for(
        row=row,
        run_db=run_db,
        source_path=source_path,
        source_sha256=source_sha256,
        target_path=None,
        target_sha256=None,
        converted_at=converted_at,
        status="skipped",
        message=message,
        conversion_meta={"target_format": "none"},
    )
    return {
        "row": row,
        "status": "skipped",
        "target_format": "none",
        "target_path": None,
        "target_sha256": None,
        "provenance": provenance,
        "message": message,
    }


def write_manifest(output_dir: Path, rows: list[dict[str, object]]) -> Path:
    manifest_path = output_dir / "embedded-conversions.json"
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_rows = []
    for result in rows:
        row = result["row"]
        manifest_rows.append(
            {
                "embedded_file_id": row["embedded_file_id"],
                "stack_name": row["stack_name"],
                "import_input": row["import_input"],
                "source_rel_path": row["rel_path"],
                "embedded_kind": row["embedded_kind"],
                "logical_kind": row["logical_kind"],
                "logical_id": row["logical_id"],
                "source_bytes": row["bytes"],
                "source_sha256": row["sha256"],
                "status": result["status"],
                "target_format": result["target_format"],
                "target_path": None if result["target_path"] is None else str(result["target_path"]),
                "target_sha256": result["target_sha256"],
                "message": result["message"],
            }
        )
    manifest_path.write_text(json.dumps(manifest_rows, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return manifest_path


def write_run_provenance(output_dir: Path, run_db: Path, rows: list[dict[str, object]]) -> Path:
    status_counts: dict[str, int] = {}
    for result in rows:
        status = str(result["status"])
        status_counts[status] = status_counts.get(status, 0) + 1
    payload = {
        "converter": {
            "name": CONVERTER_NAME,
            "version": CONVERTER_VERSION,
            "repository": str(REPO_ROOT),
        },
        "run": {
            "run_db": str(run_db),
            "output_dir": str(output_dir),
            "converted_at": datetime.now(timezone.utc).isoformat(),
            "processed": len(rows),
            "status_counts": status_counts,
        },
    }
    path = output_dir / "conversion-run.provenance.json"
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return path


def main() -> int:
    args = parse_args()
    run_db = args.run_db or latest_run_db(args.run_root)
    run_db = run_db.resolve()
    run_dir = run_db.parent
    output_dir = (args.output_dir or (run_dir / "embedded-conversions")).resolve()

    conn = connect_database(run_db)
    if not args.no_db:
        ensure_conversion_table(conn)
    rows = query_embedded_files(conn, args)

    results = []
    for row in rows:
        result = convert_row(row, run_db, output_dir)
        results.append(result)
        if not args.no_db:
            upsert_conversion(
                conn,
                row,
                Path(row["source_path"] or ""),
                result["provenance"]["source"]["sha256"],
                result["target_path"],
                result["target_format"],
                result["target_sha256"],
                result["status"],
                result["provenance"]["converter"]["converted_at"],
                result["provenance"],
                result["message"],
            )
    if not args.no_db:
        conn.commit()
    conn.close()

    manifest_path = write_manifest(output_dir, results)
    provenance_path = write_run_provenance(output_dir, run_db, results)

    status_counts: dict[str, int] = {}
    for result in results:
        status = str(result["status"])
        status_counts[status] = status_counts.get(status, 0) + 1

    print(f"Run database: {run_db}")
    print(f"Output directory: {output_dir}")
    print(f"Manifest: {manifest_path}")
    print(f"Run provenance: {provenance_path}")
    print(f"Processed: {len(results)}")
    for status, count in sorted(status_counts.items()):
        print(f"{status}: {count}")
    return 1 if status_counts.get("failed", 0) else 0


if __name__ == "__main__":
    sys.exit(main())
