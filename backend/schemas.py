from datetime import datetime
from typing import Literal, Optional

from pydantic import BaseModel, Field, model_validator

CODE_PATTERN = r"^[A-Za-z0-9._:/-]{4,64}$"   # same charset/length rule as the firmware


class _DeviceRecord(BaseModel):
    model_config = {"extra": "forbid"}
    boot_id: int = Field(ge=0, le=2**32 - 1)
    seq: int = Field(ge=0, le=2**32 - 1)
    uptime_ms: int = Field(ge=0)


class ReadingIn(_DeviceRecord):
    recorded_at: Optional[datetime] = None
    heart_rate: Optional[int] = Field(None, ge=20, le=250)
    spo2: Optional[int] = Field(None, ge=50, le=100)
    temperature_c: Optional[float] = Field(None, ge=25.0, le=45.0)

    @model_validator(mode="after")
    def _has_metric(self):
        if self.heart_rate is None and self.spo2 is None and self.temperature_c is None:
            raise ValueError("at least one of heart_rate, spo2, temperature_c is required")
        return self


class ScanIn(_DeviceRecord):
    scanned_at: Optional[datetime] = None
    medicine_code: str = Field(pattern=CODE_PATTERN)
    symbology: Literal["QR", "EAN13", "CODE128", "DATAMATRIX", "UNKNOWN"] = "UNKNOWN"


class MedicineIn(BaseModel):
    model_config = {"extra": "forbid"}
    name: str = Field(min_length=1, max_length=120)
    strength: Optional[str] = Field(None, max_length=60)
    notes: Optional[str] = Field(None, max_length=500)


class PatientIn(BaseModel):
    model_config = {"extra": "forbid"}
    display_name: str = Field(min_length=1, max_length=80)


class AssignIn(BaseModel):
    model_config = {"extra": "forbid"}
    patient_id: str = Field(min_length=1, max_length=64)
