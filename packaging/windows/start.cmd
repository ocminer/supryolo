@echo off
rem SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
setlocal
rem Replace ONLY YOUR_BTCB2_ADDRESS with your own payout address.
if not defined WALLET set "WALLET=YOUR_BTCB2_ADDRESS"
if "%WALLET%"=="YOUR_BTCB2_ADDRESS" (
  echo Edit start.cmd and replace YOUR_BTCB2_ADDRESS with your BTCB2 address.
  pause
  exit /b 2
)
if not defined WORKER set "WORKER=rig1"
set "SSL_CERT_FILE=%~dp0ca-bundle.crt"
"%~dp0supryolo.exe" --url stratum+tcp://de.b2pool.io:4444 --user "%WALLET%.%WORKER%" --no-cpu %*
pause
