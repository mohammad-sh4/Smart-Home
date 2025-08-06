@echo off
echo Smart Home Flutter App Setup Script
echo ====================================
echo.

echo 1. Installing Flutter dependencies...
call flutter pub get
if %errorlevel% neq 0 (
    echo ERROR: Failed to install dependencies
    pause
    exit /b 1
)

echo.
echo 2. Cleaning previous builds...
call flutter clean

echo.
echo 3. Getting dependencies again...
call flutter pub get

echo.
echo 4. Checking Flutter doctor...
call flutter doctor

echo.
echo 5. Setup complete!
echo.
echo Next steps:
echo - Connect your Android device or start an emulator
echo - Make sure your ESP32 is running and connected to WiFi
echo - Run: flutter run
echo - Or run: flutter run --release (for release mode)
echo.
pause
