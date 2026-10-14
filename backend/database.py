"""SQLite layer. Append-only history: every reading and every scan is a new row."""
import os
import sqlite3

DB_PATH = os.getenv("AEGIS_DB", "aegismed.db")

SCHEMA = """
CREATE TABLE IF NOT EXISTS patients (
    id TEXT PRIMARY KEY,
    display_name TEXT
);
CREATE TABLE IF NOT EXISTS devices (
    id TEXT PRIMARY KEY,
    patient_id TEXT REFERENCES patients(id),
    first_seen_at TEXT NOT NULL,
    last_seen_at  TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS readings (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id TEXT NOT NULL REFERENCES devices(id),
    boot_id INTEGER NOT NULL,
    seq INTEGER NOT NULL,
    uptime_ms INTEGER NOT NULL,
    recorded_at TEXT,              -- device clock (UTC), NULL if SNTP was not synced yet
    effective_at TEXT NOT NULL,    -- recorded_at, else server receive time
    received_at TEXT NOT NULL,
    heart_rate INTEGER,
    spo2 INTEGER,
    temperature_c REAL,
    UNIQUE (device_id, boot_id, seq)           -- idempotency key
);
CREATE INDEX IF NOT EXISTS idx_readings_dev_time ON readings (device_id, effective_at DESC);
CREATE TABLE IF NOT EXISTS medicines (
    code TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    strength TEXT,
    notes TEXT
);
CREATE TABLE IF NOT EXISTS medicine_scans (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id TEXT NOT NULL REFERENCES devices(id),
    boot_id INTEGER NOT NULL,
    seq INTEGER NOT NULL,
    uptime_ms INTEGER NOT NULL,
    scanned_at TEXT,
    effective_at TEXT NOT NULL,
    received_at TEXT NOT NULL,
    medicine_code TEXT NOT NULL,   -- deliberately NOT a FK: unknown codes are still recorded
    symbology TEXT NOT NULL,
    UNIQUE (device_id, boot_id, seq)
);
CREATE INDEX IF NOT EXISTS idx_scans_dev_time ON medicine_scans (device_id, effective_at DESC);
"""

# Demo catalog entries for testing only.
SEED = [
    ("AEGIS-PARA-500", "Paracetamol", "500 mg", "demo entry"),
    ("AEGIS-IBU-400", "Ibuprofen", "400 mg", "demo entry"),
    ("8901234567890", "Demo medicine (EAN-13)", None, "demo entry"),
]


def _connect() -> sqlite3.Connection:
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA foreign_keys = ON")
    return conn


def init_db() -> None:
    conn = _connect()
    conn.execute("PRAGMA journal_mode = WAL")
    conn.executescript(SCHEMA)
    conn.executemany("INSERT OR IGNORE INTO medicines(code,name,strength,notes) VALUES (?,?,?,?)", SEED)
    conn.commit()
    conn.close()


def get_db():
    """FastAPI dependency: one connection per request."""
    conn = _connect()
    try:
        yield conn
    finally:
        conn.close()
