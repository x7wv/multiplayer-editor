@echo off
title Multiplayer Editor server (by x7wv)
cd /d "%~dp0"
where node >nul 2>nul || (echo Node.js is not installed. Get the LTS version from https://nodejs.org then run this again. & pause & exit /b)
if not exist node_modules (echo Installing for the first time... & call npm install --omit=dev)
echo.
echo Server starting. Keep this window open while people are building!
echo Local address: ws://127.0.0.1:8080
echo.
node server.js
pause
