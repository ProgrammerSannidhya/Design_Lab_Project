"""AEGISMED backend. Run: uvicorn main:app --host 0.0.0.0 --port 8000

Prototype security: per-device key (X-Device-Key) for ingestion, optional bearer token for reads/admin,
plain HTTP. Production needs TLS, real user auth, per-device provisioning, audit logging.
Not a medical device.
"""
import hmac
import logging
import os
from contextlib import asynccontextmanager
from datetime import datetime, timedelta, timezone
from typing import Annotated, Optional

from fastapi import Depends, FastAPI, Header, HTTPException, Path, Query, Response

from database import get_db, init_db
from schemas import AssignIn, CODE_PATTERN, MedicineIn, PatientIn, ReadingIn, ScanIn

log = logging.getLogger("aegismed")
ONLINE_WINDOW_S = int(os.getenv("AEGIS_ONLINE_WINDOW_S", "60"))
APP_TOKEN = os.getenv("AEGIS_APP_TOKEN", "")   # empty = reads are open (dev only)
MAX_BATCH = 100


def _load_device_keys() -> dict:
    raw = os.getenv("AEGIS_DEVICE_KEYS", "")          # "aegis-001:secret,aegis-002:secret2"
    keys = dict(p.split(":", 1) for p in raw.split(",") if ":" in p)
    if not keys:
        log.warning("AEGIS_DEVICE_KEYS not set: using INSECURE dev key for device aegis-001")
        keys = {"aegis-001": "dev-key-change-me"}
    return keys


DEVICE_KEYS = _load_device_keys()


def now_iso() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def to_iso(dt: Optional[datetime]) -> Optional[str]:
    if dt is None:
        return None
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=timezone.utc)
    return dt.astimezone(timezone.utc).isoformat(timespec="milliseconds")


def auth_device(device_id: str, x_device_key: str = Header(default="")) -> str:
    expected = DEVICE_KEYS.get(device_id)
    if expected is None or not hmac.compare_digest(expected.encode(), x_device_key.encode()):
        raise HTTPException(401, "invalid device credentials")
    return device_id


def auth_app(authorization: str = Header(default="")) -> None:
    if APP_TOKEN and not hmac.compare_digest(authorization.encode(), f"Bearer {APP_TOKEN}".encode()):
        raise HTTPException(401, "invalid or missing bearer token")


def touch_device(db, device_id: str, now: str) -> None:
    db.execute(
        "INSERT INTO devices(id, first_seen_at, last_seen_at) VALUES (?,?,?) "
        "ON CONFLICT(id) DO UPDATE SET last_seen_at = excluded.last_seen_at",
        (device_id, now, now),
    )


@asynccontextmanager
async def lifespan(app: FastAPI):
    init_db()
    yield


app = FastAPI(title="AEGISMED API", version="0.1.0", lifespan=lifespan)
App = [Depends(auth_app)]


# ------------------------------------------------------------------ ingestion (device -> backend)
@app.post("/api/v1/devices/{device_id}/readings", status_code=201)
def post_readings(device_id: str, body: ReadingIn | list[ReadingIn], response: Response,
                  _=Depends(auth_device), db=Depends(get_db)):
    items = body if isinstance(body, list) else [body]
    if not 1 <= len(items) <= MAX_BATCH:
        raise HTTPException(422, f"batch size must be 1..{MAX_BATCH}")
    now = now_iso()
    touch_device(db, device_id, now)
    inserted = 0
    for r in items:
        cur = db.execute(
            "INSERT INTO readings(device_id,boot_id,seq,uptime_ms,recorded_at,effective_at,received_at,"
            "heart_rate,spo2,temperature_c) VALUES (?,?,?,?,?,?,?,?,?,?) "
            "ON CONFLICT(device_id,boot_id,seq) DO NOTHING",
            (device_id, r.boot_id, r.seq, r.uptime_ms, to_iso(r.recorded_at), to_iso(r.recorded_at) or now, now,
             r.heart_rate, r.spo2, r.temperature_c),
        )
        inserted += cur.rowcount
    db.commit()
    if inserted == 0:
        response.status_code = 200
    return {"status": "created" if inserted else "duplicate", "inserted": inserted, "duplicates": len(items) - inserted}


@app.post("/api/v1/devices/{device_id}/medicine-scans", status_code=201)
def post_scans(device_id: str, body: ScanIn | list[ScanIn], response: Response,
               _=Depends(auth_device), db=Depends(get_db)):
    items = body if isinstance(body, list) else [body]
    if not 1 <= len(items) <= MAX_BATCH:
        raise HTTPException(422, f"batch size must be 1..{MAX_BATCH}")
    now = now_iso()
    touch_device(db, device_id, now)
    inserted = 0
    for s in items:
        cur = db.execute(
            "INSERT INTO medicine_scans(device_id,boot_id,seq,uptime_ms,scanned_at,effective_at,received_at,"
            "medicine_code,symbology) VALUES (?,?,?,?,?,?,?,?,?) ON CONFLICT(device_id,boot_id,seq) DO NOTHING",
            (device_id, s.boot_id, s.seq, s.uptime_ms, to_iso(s.scanned_at), to_iso(s.scanned_at) or now, now,
             s.medicine_code, s.symbology),
        )
        inserted += cur.rowcount
    db.commit()
    if inserted == 0:
        response.status_code = 200
    return {"status": "created" if inserted else "duplicate", "inserted": inserted, "duplicates": len(items) - inserted}


# ------------------------------------------------------------------ queries (application -> backend)
def _readings(db, device_ids, since, limit):
    q = ",".join("?" * len(device_ids))
    sql = (f"SELECT id, device_id, recorded_at, effective_at, received_at, boot_id, seq, uptime_ms, heart_rate, spo2, "
           f"temperature_c FROM readings WHERE device_id IN ({q})")
    args = list(device_ids)
    if since:
        sql += " AND effective_at > ?"
        args.append(to_iso(since))
    sql += " ORDER BY effective_at DESC, id DESC LIMIT ?"
    return [dict(r) for r in db.execute(sql, args + [limit]).fetchall()]


def _scans(db, device_ids, limit):
    q = ",".join("?" * len(device_ids))
    sql = (f"SELECT s.id, s.device_id, s.effective_at, s.scanned_at, s.received_at, s.medicine_code, s.symbology, "
           f"m.name AS medicine_name, m.strength AS medicine_strength, (m.code IS NOT NULL) AS known "
           f"FROM medicine_scans s LEFT JOIN medicines m ON m.code = s.medicine_code "
           f"WHERE s.device_id IN ({q}) ORDER BY s.effective_at DESC, s.id DESC LIMIT ?")
    rows = [dict(r) for r in db.execute(sql, list(device_ids) + [limit]).fetchall()]
    for r in rows:
        r["known"] = bool(r["known"])
    return rows


@app.get("/api/v1/devices/{device_id}/readings", dependencies=App)
def list_readings(device_id: str, since: Optional[datetime] = None, limit: int = Query(100, ge=1, le=1000),
                  db=Depends(get_db)):
    return {"device_id": device_id, "readings": _readings(db, [device_id], since, limit)}


@app.get("/api/v1/devices/{device_id}/readings/latest", dependencies=App)
def latest_reading(device_id: str, db=Depends(get_db)):
    rows = _readings(db, [device_id], None, 1)
    if not rows:
        raise HTTPException(404, "no readings for device")
    return rows[0]


@app.get("/api/v1/devices/{device_id}/medicine-scans", dependencies=App)
def list_scans(device_id: str, limit: int = Query(50, ge=1, le=500), db=Depends(get_db)):
    return {"device_id": device_id, "medicine_scans": _scans(db, [device_id], limit)}


@app.get("/api/v1/devices/{device_id}/status", dependencies=App)
def device_status(device_id: str, db=Depends(get_db)):
    dev = db.execute("SELECT * FROM devices WHERE id=?", (device_id,)).fetchone()
    if not dev:
        raise HTTPException(404, "unknown device")
    age = datetime.now(timezone.utc) - datetime.fromisoformat(dev["last_seen_at"])
    reading = _readings(db, [device_id], None, 1)
    scan = _scans(db, [device_id], 1)
    n_r = db.execute("SELECT COUNT(*) FROM readings WHERE device_id=?", (device_id,)).fetchone()[0]
    n_s = db.execute("SELECT COUNT(*) FROM medicine_scans WHERE device_id=?", (device_id,)).fetchone()[0]
    return {"device_id": device_id, "patient_id": dev["patient_id"], "online": age <= timedelta(seconds=ONLINE_WINDOW_S),
            "last_seen_at": dev["last_seen_at"], "reading_count": n_r, "scan_count": n_s,
            "latest_reading": reading[0] if reading else None, "latest_scan": scan[0] if scan else None}


@app.get("/api/v1/medicines/{code}", dependencies=App)
def get_medicine(code: Annotated[str, Path(pattern=CODE_PATTERN)], db=Depends(get_db)):
    row = db.execute("SELECT * FROM medicines WHERE code=?", (code,)).fetchone()
    if not row:
        raise HTTPException(404, "medicine not in catalog")
    return dict(row)


@app.put("/api/v1/medicines/{code}", dependencies=App)
def put_medicine(code: Annotated[str, Path(pattern=CODE_PATTERN)], body: MedicineIn, db=Depends(get_db)):
    db.execute("INSERT INTO medicines(code,name,strength,notes) VALUES (?,?,?,?) ON CONFLICT(code) DO UPDATE SET "
               "name=excluded.name, strength=excluded.strength, notes=excluded.notes",
               (code, body.name, body.strength, body.notes))
    db.commit()
    return {"code": code, **body.model_dump()}


@app.put("/api/v1/patients/{patient_id}", dependencies=App)
def put_patient(patient_id: str, body: PatientIn, db=Depends(get_db)):
    db.execute("INSERT INTO patients(id,display_name) VALUES (?,?) ON CONFLICT(id) DO UPDATE SET "
               "display_name=excluded.display_name", (patient_id, body.display_name))
    db.commit()
    return {"id": patient_id, "display_name": body.display_name}


@app.put("/api/v1/devices/{device_id}/patient", dependencies=App)
def assign_device(device_id: str, body: AssignIn, db=Depends(get_db)):
    if not db.execute("SELECT 1 FROM patients WHERE id=?", (body.patient_id,)).fetchone():
        raise HTTPException(404, "unknown patient")
    now = now_iso()
    touch_device(db, device_id, now)
    db.execute("UPDATE devices SET patient_id=? WHERE id=?", (body.patient_id, device_id))
    db.commit()
    return {"device_id": device_id, "patient_id": body.patient_id}


@app.get("/api/v1/patients/{patient_id}/history", dependencies=App)
def patient_history(patient_id: str, limit: int = Query(100, ge=1, le=1000), db=Depends(get_db)):
    if not db.execute("SELECT 1 FROM patients WHERE id=?", (patient_id,)).fetchone():
        raise HTTPException(404, "unknown patient")
    ids = [r["id"] for r in db.execute("SELECT id FROM devices WHERE patient_id=?", (patient_id,)).fetchall()]
    if not ids:
        return {"patient_id": patient_id, "devices": [], "readings": [], "medicine_scans": []}
    return {"patient_id": patient_id, "devices": ids, "readings": _readings(db, ids, None, limit),
            "medicine_scans": _scans(db, ids, limit)}


@app.get("/health")
def health():
    return {"status": "ok"}
