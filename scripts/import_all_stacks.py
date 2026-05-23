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
from xml.etree import ElementTree


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
            rel_path TEXT NOT NULL,
            path TEXT NOT NULL,
            bytes INTEGER NOT NULL,
            extension TEXT NOT NULL,
            kind TEXT NOT NULL,
            sha256 TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS xml_sections (
            id INTEGER PRIMARY KEY,
            attempt_id INTEGER NOT NULL REFERENCES import_attempts(id) ON DELETE CASCADE,
            output_file_id INTEGER REFERENCES output_files(id) ON DELETE CASCADE,
            file_rel_path TEXT NOT NULL,
            depth INTEGER NOT NULL,
            tag TEXT NOT NULL,
            attrs_json TEXT NOT NULL,
            text_size INTEGER NOT NULL
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
        CREATE INDEX IF NOT EXISTS idx_import_attempts_run ON import_attempts(run_id);
        CREATE INDEX IF NOT EXISTS idx_diagnostics_attempt ON diagnostics(attempt_id);
        CREATE INDEX IF NOT EXISTS idx_output_files_attempt ON output_files(attempt_id);
        CREATE INDEX IF NOT EXISTS idx_xml_sections_tag ON xml_sections(tag);
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


def output_package_for(import_path: Path) -> Path:
    path_text = str(import_path)
    stak_pos = path_text.rfind(".stak")
    if stak_pos != -1:
        return Path(f"{path_text[:stak_pos]}.xstk")
    return Path(f"{path_text}.xstk")


def kind_for_output(path: Path) -> str:
    suffix = path.suffix.lower()
    if suffix == ".xml":
        return "xml"
    if suffix in {".pbm", ".png", ".jpg", ".jpeg", ".gif"}:
        return "image"
    if suffix in {".aiff", ".aif", ".wav", ".snd"}:
        return "sound"
    if suffix in {".raw", ".data"}:
        return "raw"
    return "other"


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


def index_output_package(conn: sqlite3.Connection, attempt_id: int, output_package: Path) -> None:
    for output_file in sorted((path for path in output_package.rglob("*") if path.is_file()), key=lambda item: str(item)):
        rel_path = str(output_file.relative_to(output_package))
        cursor = conn.execute(
            """
            INSERT INTO output_files(attempt_id, rel_path, path, bytes, extension, kind, sha256)
            VALUES (?, ?, ?, ?, ?, ?, ?)
            """,
            (
                attempt_id,
                rel_path,
                str(output_file),
                output_file.stat().st_size,
                extension_for(output_file),
                kind_for_output(output_file),
                file_sha256(output_file),
            ),
        )
        output_file_id = int(cursor.lastrowid)
        if output_file.suffix.lower() == ".xml":
            index_xml_sections(conn, attempt_id, output_file_id, rel_path, output_file)


def index_xml_sections(
    conn: sqlite3.Connection,
    attempt_id: int,
    output_file_id: int,
    rel_path: str,
    output_file: Path,
) -> None:
    try:
        tree = ElementTree.parse(output_file)
    except ElementTree.ParseError as error:
        insert_format_gap(
            conn,
            conn.execute("SELECT run_id FROM import_attempts WHERE id = ?", (attempt_id,)).fetchone()["run_id"],
            "generated_xml_parse",
            str(error),
            rel_path,
            attempt_id=attempt_id,
            log_path=output_file,
        )
        return

    def visit(element: ElementTree.Element, depth: int) -> None:
        text_size = len(element.text or "")
        conn.execute(
            """
            INSERT INTO xml_sections(attempt_id, output_file_id, file_rel_path, depth, tag, attrs_json, text_size)
            VALUES (?, ?, ?, ?, ?, ?, ?)
            """,
            (attempt_id, output_file_id, rel_path, depth, element.tag, json.dumps(element.attrib, sort_keys=True), text_size),
        )
        for child in list(element):
            visit(child, depth + 1)

    visit(tree.getroot(), 0)


def write_tsv(path: Path, rows: list[dict[str, object]], fieldnames: list[str]) -> None:
    with path.open("w", newline="", encoding="utf-8") as file:
        writer = csv.DictWriter(file, fieldnames=fieldnames, dialect="excel-tab", extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def export_reports(conn: sqlite3.Connection, run_dir: Path, run_id: str) -> None:
    report_queries = {
        "summary.tsv": (
            """
            SELECT status, exit_code, warnings, errors, status_lines, blocks, resources,
                   archive_input, import_input, log_path, output_package
            FROM import_attempts
            WHERE run_id = ?
            ORDER BY id
            """,
            [
                "status",
                "exit_code",
                "warnings",
                "errors",
                "status_lines",
                "blocks",
                "resources",
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
            SELECT a.import_input, f.rel_path, f.bytes, f.extension, f.kind, f.sha256
            FROM output_files f
            JOIN import_attempts a ON a.id = f.attempt_id
            WHERE a.run_id = ?
            ORDER BY a.import_input, f.rel_path
            """,
            ["import_input", "rel_path", "bytes", "extension", "kind", "sha256"],
        ),
        "xml-sections.tsv": (
            """
            SELECT a.import_input, x.file_rel_path, x.depth, x.tag, x.attrs_json, x.text_size
            FROM xml_sections x
            JOIN import_attempts a ON a.id = x.attempt_id
            WHERE a.run_id = ?
            ORDER BY a.import_input, x.file_rel_path, x.id
            """,
            ["import_input", "file_rel_path", "depth", "tag", "attrs_json", "text_size"],
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
    print(f"XML sections: {run_dir / 'xml-sections.tsv'}")
    print(
        f"Processed: {processed}, stack ok: {stack_ok}, stack failed: {stack_failed}, "
        f"no-stack inputs: {no_stack}, extract failed: {extract_failed}"
    )
    return 1 if stack_failed or extract_failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
