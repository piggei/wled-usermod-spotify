from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
runner = root / "tools" / "test_runner.sh"

with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    artifact_dir = td / "artifacts"
    artifact_dir.mkdir()
    firmware = artifact_dir / "firmware.bin"
    elf = artifact_dir / "firmware.elf"
    firmware.write_bytes(b"\x00BIN_ONLY\x00")
    elf.write_bytes(b"\x7fELF\x00POSITIVE_MARKER\x00")
    manifest = td / "checks.tsv"
    manifest.write_text(
        "ID\tPHASE\tLEVEL\tTYPE\tTARGET\tEXPECTED\tDESCRIPTION\n"
        "YES\tpostbuild\trequired\tfirmware_contains\tfirmware\tPOSITIVE_MARKER\tpositive raw search\n"
        "NO\tpostbuild\trequired\tfirmware_not_contains\tfirmware\tABSENT_MARKER\tnegative raw search\n"
    )
    subprocess.run([
        "bash", str(runner), "--manifest", str(manifest), "--repo", str(root),
        "--phase", "postbuild", "--firmware", str(firmware)
    ], check=True, stdout=subprocess.DEVNULL)

print("firmware raw-search runner regression: PASS")
