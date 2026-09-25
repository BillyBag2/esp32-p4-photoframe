"""Google Photos test client for desktop Picker and TV Ambient API flows.

Setup:
  1. Enable the Google Photos Picker API in a Google Cloud project.
  2. Create an OAuth 2.0 client of type "Desktop app" or
     "TVs and Limited Input devices", then download its JSON.
  3. Install dependencies:
       python -m pip install google-auth google-auth-oauthlib requests
  4. Run:
       python photo_picker.py --client-secrets client_secret.json

The OAuth refresh token is cached in ``token.json`` next to this script. Keep
both credential files private and out of source control.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
import uuid
import webbrowser
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Any
from urllib.parse import quote

try:
    import requests
    from google.auth.transport.requests import Request
    from google.oauth2.credentials import Credentials
    from google_auth_oauthlib.flow import InstalledAppFlow
except ImportError as exc:  # Give a useful error instead of a long traceback.
    raise SystemExit(
        "Missing dependencies. Run: python -m pip install "
        "google-auth google-auth-oauthlib requests"
    ) from exc


API_ROOT = "https://photospicker.googleapis.com/v1"
AMBIENT_API_ROOT = "https://photosambient.googleapis.com/v1"
DEVICE_CODE_URL = "https://oauth2.googleapis.com/device/code"
TOKEN_URL = "https://oauth2.googleapis.com/token"
PICKER_SCOPES = ["https://www.googleapis.com/auth/photospicker.mediaitems.readonly"]
AMBIENT_SCOPES = [
    "profile",
    "https://www.googleapis.com/auth/photosambient.mediaitems",
]
SCRIPT_DIR = Path(__file__).resolve().parent


def duration_seconds(value: str, default: float) -> float:
    """Convert a Google protobuf duration such as '3.5s' to seconds."""
    match = re.fullmatch(r"(\d+(?:\.\d+)?)s", value or "")
    return float(match.group(1)) if match else default


def read_client_config(client_secrets: Path) -> dict[str, Any]:
    try:
        document = json.loads(client_secrets.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ValueError(f"Cannot read OAuth client JSON: {client_secrets}") from exc

    config = document.get("installed") or document.get("web")
    if not config or not config.get("client_id"):
        raise ValueError(f"No OAuth client configuration found in {client_secrets}")
    return config


def detect_auth_flow(client_secrets: Path) -> str:
    config = read_client_config(client_secrets)
    if any(
        uri.startswith(("http://localhost", "http://127.0.0.1", "http://[::1]"))
        for uri in config.get("redirect_uris", [])
    ):
        return "desktop"
    return "device"


def load_credentials(
    client_secrets: Path,
    token_file: Path,
    auth_flow: str,
    request_id: str | None = None,
) -> tuple[Credentials, str]:
    if auth_flow == "auto" and client_secrets.exists():
        auth_flow = detect_auth_flow(client_secrets)
    credentials: Credentials | None = None
    scopes = AMBIENT_SCOPES if auth_flow == "device" else PICKER_SCOPES
    if token_file.exists():
        credentials = Credentials.from_authorized_user_file(str(token_file), scopes)

    if credentials and credentials.expired and credentials.refresh_token:
        credentials.refresh(Request())
    elif not credentials or not credentials.valid:
        if not client_secrets.exists():
            raise FileNotFoundError(
                f"OAuth client file not found: {client_secrets}\n"
                "Download an OAuth client JSON from Google Cloud Console."
            )
        selected_flow = auth_flow
        if selected_flow == "auto":
            selected_flow = detect_auth_flow(client_secrets)
        if selected_flow == "desktop":
            validate_desktop_client(client_secrets)
            flow = InstalledAppFlow.from_client_secrets_file(
                str(client_secrets), PICKER_SCOPES
            )
            credentials = flow.run_local_server(port=0, open_browser=True)
        else:
            credentials = authorize_limited_input_device(
                client_secrets, request_id or str(uuid.uuid4())
            )
        auth_flow = selected_flow

    token_file.parent.mkdir(parents=True, exist_ok=True)
    token_file.write_text(credentials.to_json(), encoding="utf-8")
    return credentials, auth_flow


def authorize_limited_input_device(
    client_secrets: Path, request_id: str
) -> Credentials:
    """Run Google's OAuth 2.0 flow for TVs and limited-input devices."""
    config = read_client_config(client_secrets)
    client_id = config["client_id"]
    client_secret = config.get("client_secret")
    code_response = requests.post(
        DEVICE_CODE_URL,
        json={
            "client_id": client_id,
            "scope": " ".join(AMBIENT_SCOPES),
            "state": json.dumps(
                {"requestId": request_id, "displayName": "ESP32-P4 Photo Frame"}
            ),
        },
        timeout=30,
    )
    if not code_response.ok:
        raise RuntimeError(f"Device authorization failed: {code_response.text}")
    device = code_response.json()

    verification_url = device.get("verification_url") or device.get("verification_uri")
    print("\nAuthorize this device using another browser:")
    print(f"  1. Open: {verification_url}")
    print(f"  2. Enter code: {device['user_code']}\n")

    interval = float(device.get("interval", 5))
    deadline = time.monotonic() + float(device["expires_in"])
    token_data: dict[str, Any]
    while True:
        if time.monotonic() >= deadline:
            raise TimeoutError("The device authorization code expired.")
        time.sleep(interval)
        payload = {
            "client_id": client_id,
            "device_code": device["device_code"],
            "grant_type": "urn:ietf:params:oauth:grant-type:device_code",
        }
        if client_secret:
            payload["client_secret"] = client_secret
        token_response = requests.post(TOKEN_URL, data=payload, timeout=30)
        token_data = token_response.json()
        if token_response.ok:
            break
        error = token_data.get("error")
        if error == "authorization_pending":
            continue
        if error == "slow_down":
            interval += 5
            continue
        if error == "access_denied":
            raise RuntimeError("The user denied device authorization.")
        if error == "expired_token":
            raise TimeoutError("The device authorization code expired.")
        raise RuntimeError(f"Device token request failed: {token_data}")

    return Credentials(
        token=token_data["access_token"],
        refresh_token=token_data.get("refresh_token"),
        token_uri=TOKEN_URL,
        client_id=client_id,
        client_secret=client_secret,
        scopes=token_data.get("scope", " ".join(AMBIENT_SCOPES)).split(),
        expiry=datetime.now(timezone.utc)
        + timedelta(seconds=float(token_data.get("expires_in", 3600))),
    )


def validate_desktop_client(client_secrets: Path) -> None:
    """Fail early when a mobile/web client is supplied to the desktop flow."""
    config = read_client_config(client_secrets)
    redirect_uris = config.get("redirect_uris", [])
    has_loopback_redirect = any(
        uri.startswith(("http://localhost", "http://127.0.0.1", "http://[::1]"))
        for uri in redirect_uris
    )
    if not has_loopback_redirect:
        raise ValueError(
            "This is not a Desktop app OAuth client JSON. Google blocks loopback "
            "authentication for Android, iOS, and Chrome-app client IDs. In Google "
            "Cloud Console, open Google Auth Platform > Clients, create an OAuth "
            "client with application type 'Desktop app', download its JSON, and "
            "pass that file to --client-secrets."
        )


class PhotosPicker:
    def __init__(self, credentials: Credentials) -> None:
        self.credentials = credentials
        self.http = requests.Session()

    def _headers(self) -> dict[str, str]:
        if not self.credentials.valid:
            self.credentials.refresh(Request())
        return {"Authorization": f"Bearer {self.credentials.token}"}

    def _request(self, method: str, url: str, **kwargs: Any) -> requests.Response:
        headers = dict(kwargs.pop("headers", {}))
        headers.update(self._headers())
        response = self.http.request(method, url, headers=headers, timeout=60, **kwargs)
        try:
            response.raise_for_status()
        except requests.HTTPError as exc:
            try:
                detail = response.json()
            except ValueError:
                detail = response.text
            raise RuntimeError(f"Google Photos API error: {detail}") from exc
        return response

    def create_session(
        self, max_items: int | None = None, request_id: str | None = None
    ) -> dict[str, Any]:
        body: dict[str, Any] = {}
        if max_items is not None:
            body["pickingConfig"] = {"maxItemCount": str(max_items)}
        params = {"requestId": request_id} if request_id else None
        return self._request(
            "POST", f"{API_ROOT}/sessions", params=params, json=body
        ).json()

    def wait_for_selection(self, session: dict[str, Any]) -> dict[str, Any]:
        session_id = quote(session["id"], safe="")
        config = session.get("pollingConfig", {})
        timeout = duration_seconds(config.get("timeoutIn", ""), 600.0)
        deadline = time.monotonic() + timeout

        while not session.get("mediaItemsSet", False):
            interval = duration_seconds(config.get("pollInterval", ""), 3.0)
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("The photo-picking session timed out.")
            time.sleep(min(interval, remaining))
            session = self._request(
                "GET", f"{API_ROOT}/sessions/{session_id}"
            ).json()
            config = session.get("pollingConfig", config)
        return session

    def list_media_items(self, session_id: str) -> list[dict[str, Any]]:
        items: list[dict[str, Any]] = []
        page_token: str | None = None
        while True:
            params = {"sessionId": session_id, "pageSize": 100}
            if page_token:
                params["pageToken"] = page_token
            page = self._request(
                "GET", f"{API_ROOT}/mediaItems", params=params
            ).json()
            items.extend(page.get("mediaItems", []))
            page_token = page.get("nextPageToken")
            if not page_token:
                return items

    def download(self, item: dict[str, Any], output_dir: Path) -> Path:
        media_file = item["mediaFile"]
        filename = Path(media_file.get("filename") or item["id"]).name
        destination = unique_path(output_dir / filename)
        parameter = "=dv" if item.get("type") == "VIDEO" else "=d"

        output_dir.mkdir(parents=True, exist_ok=True)
        with self._request(
            "GET", media_file["baseUrl"] + parameter, stream=True
        ) as response, destination.open("wb") as output:
            for chunk in response.iter_content(chunk_size=1024 * 1024):
                if chunk:
                    output.write(chunk)
        return destination

    def delete_session(self, session_id: str) -> None:
        encoded_id = quote(session_id, safe="")
        self._request("DELETE", f"{API_ROOT}/sessions/{encoded_id}")


class AmbientPhotos(PhotosPicker):
    """Google Photos client for TVs, photo frames, and limited-input devices."""

    def create_device(self, request_id: str) -> dict[str, Any]:
        return self._request(
            "POST",
            f"{AMBIENT_API_ROOT}/devices",
            params={"requestId": request_id},
            json={"displayName": "ESP32-P4 Photo Frame"},
        ).json()

    def get_device(self, device_id: str) -> dict[str, Any]:
        encoded_id = quote(device_id, safe="")
        return self._request(
            "GET", f"{AMBIENT_API_ROOT}/devices/{encoded_id}"
        ).json()

    def wait_for_media_sources(self, device: dict[str, Any]) -> dict[str, Any]:
        while not device.get("mediaSourcesSet", False):
            interval = duration_seconds(
                device.get("pollingConfig", {}).get("pollInterval", ""), 5.0
            )
            time.sleep(interval)
            device = self.get_device(device["id"])
        return device

    def list_ambient_items(
        self, device_id: str, page_size: int = 100
    ) -> list[dict[str, Any]]:
        items: list[dict[str, Any]] = []
        page_token: str | None = None
        while True:
            params: dict[str, Any] = {
                "deviceId": device_id,
                "pageSize": min(page_size, 100),
            }
            if page_token:
                params["pageToken"] = page_token
            page = self._request(
                "GET", f"{AMBIENT_API_ROOT}/mediaItems", params=params
            ).json()
            items.extend(page.get("mediaItems", []))
            page_token = page.get("nextPageToken")
            # Ambient pagination may repeat indefinitely for the curated feed.
            if not page_token or len(items) >= page_size:
                return items[:page_size]

    def download(self, item: dict[str, Any], output_dir: Path) -> Path:
        media_file = item["mediaFile"]
        mime_type = media_file.get("mimeType", "image/jpeg")
        extension = {"image/png": ".png", "image/webp": ".webp"}.get(
            mime_type, ".jpg"
        )
        destination = unique_path(output_dir / f"{item['id']}{extension}")
        output_dir.mkdir(parents=True, exist_ok=True)
        with self._request(
            "GET", media_file["baseUrl"] + "=d", stream=True
        ) as response, destination.open("wb") as output:
            for chunk in response.iter_content(chunk_size=1024 * 1024):
                if chunk:
                    output.write(chunk)
        return destination


def unique_path(path: Path) -> Path:
    """Avoid overwriting a file when two picked items share a filename."""
    if not path.exists():
        return path
    for number in range(1, 10_000):
        candidate = path.with_name(f"{path.stem}-{number}{path.suffix}")
        if not candidate.exists():
            return candidate
    raise FileExistsError(f"Could not find an unused filename for {path.name}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--client-secrets",
        type=Path,
        default=SCRIPT_DIR / "client_secret.json",
        help="Desktop or TVs/Limited Input OAuth client JSON",
    )
    parser.add_argument(
        "--auth-flow",
        choices=("auto", "desktop", "device"),
        default="auto",
        help="OAuth flow to use (default: infer it from the client JSON)",
    )
    parser.add_argument(
        "--token-file",
        type=Path,
        default=SCRIPT_DIR / "token.json",
        help="Location used to cache the user's OAuth token",
    )
    parser.add_argument(
        "--device-file",
        type=Path,
        default=SCRIPT_DIR / "ambient_device.json",
        help="Location used to remember the Google Ambient device ID",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=SCRIPT_DIR / "picked_photos",
        help="Directory in which selected photos and videos are downloaded",
    )
    parser.add_argument(
        "--max-items",
        type=int,
        default=None,
        help="Maximum number of selectable items (1-2000; default: 2000)",
    )
    args = parser.parse_args()
    if args.max_items is not None and not 1 <= args.max_items <= 2000:
        parser.error("--max-items must be between 1 and 2000")
    return args


def main() -> int:
    args = parse_args()
    picker: PhotosPicker | None = None
    session: dict[str, Any] | None = None

    try:
        selected_flow = (
            detect_auth_flow(args.client_secrets)
            if args.auth_flow == "auto" and args.client_secrets.exists()
            else args.auth_flow
        )
        request_id = str(uuid.uuid4()) if selected_flow == "device" else None
        credentials, auth_flow = load_credentials(
            args.client_secrets, args.token_file, args.auth_flow, request_id
        )
        if auth_flow == "device":
            ambient = AmbientPhotos(credentials)
            if args.device_file.exists():
                saved_device = json.loads(args.device_file.read_text(encoding="utf-8"))
                device = ambient.get_device(saved_device["id"])
            else:
                device = ambient.create_device(request_id or str(uuid.uuid4()))
                args.device_file.parent.mkdir(parents=True, exist_ok=True)
                args.device_file.write_text(
                    json.dumps({"id": device["id"]}, indent=2), encoding="utf-8"
                )

            if not device.get("mediaSourcesSet", False):
                settings_uri = device["settingsUri"]
                print(f"Choose albums for this photo frame:\n{settings_uri}")
                if not webbrowser.open(settings_uri):
                    print("The browser did not open automatically; open the URL above.")
                device = ambient.wait_for_media_sources(device)

            page_size = args.max_items or 100
            items = ambient.list_ambient_items(device["id"], page_size)
            print(f"Downloading {len(items)} ambient item(s) to {args.output} ...")
            for item in items:
                saved = ambient.download(item, args.output)
                print(f"  {saved}")
            metadata_file = args.output / "selection.json"
            args.output.mkdir(parents=True, exist_ok=True)
            metadata_file.write_text(json.dumps(items, indent=2), encoding="utf-8")
            print(f"Selection metadata: {metadata_file}")
            return 0

        picker = PhotosPicker(credentials)
        session = picker.create_session(args.max_items)
        picker_uri = session["pickerUri"]
        print(f"Choose photos in your browser:\n{picker_uri}")
        if not webbrowser.open(picker_uri):
            print("The browser did not open automatically; open the URL above.")

        completed = picker.wait_for_selection(session)
        items = picker.list_media_items(completed["id"])
        print(f"Downloading {len(items)} selected item(s) to {args.output} ...")
        for item in items:
            saved = picker.download(item, args.output)
            print(f"  {saved}")

        metadata_file = args.output / "selection.json"
        args.output.mkdir(parents=True, exist_ok=True)
        metadata_file.write_text(json.dumps(items, indent=2), encoding="utf-8")
        print(f"Selection metadata: {metadata_file}")
        return 0
    except (
        FileNotFoundError,
        ValueError,
        RuntimeError,
        TimeoutError,
        requests.RequestException,
    ) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1
    finally:
        if session is not None and picker is not None:
            try:
                picker.delete_session(session["id"])
            except Exception as exc:  # Cleanup must not hide the primary result.
                print(f"Warning: could not delete Picker session: {exc}", file=sys.stderr)


if __name__ == "__main__":
    raise SystemExit(main())

