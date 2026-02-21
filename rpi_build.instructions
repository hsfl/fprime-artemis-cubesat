# Raspberry Pi Native Build Instructions (Manual)

Use this path for Pi Zero W and other constrained Pi targets.  
Build directly on the Raspberry Pi, then run locally.

## 1) Find Raspberry Pi IP (from laptop)

```bash
ping -c 1 raspberrypi.local
```

If needed:

```bash
arp -a | grep -i raspberry
```

## 2) SSH into Pi

```bash
ssh pi@<PI_IP>
```

## 3) Configure UART once on fresh Pi

```bash
sudo raspi-config nonint do_serial_hw 0
sudo raspi-config nonint do_serial_cons 1
sudo systemctl disable --now serial-getty@serial0.service || true
sudo reboot
```

SSH back in after reboot.

## 4) Install build dependencies on Pi

```bash
sudo apt update
sudo apt install -y git python3 python3-venv python3-pip build-essential cmake
```

## 5) Get repo onto Pi

If repo is not already on Pi:

```bash
cd /home/pi
git clone <YOUR_REPO_URL> fprime-artemis-cubesat
```

If already present:

```bash
cd /home/pi/fprime-artemis-cubesat
git pull
```

## 6) Create/refresh Python venv for F' tools

```bash
cd /home/pi/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
python3 -m venv fprime-venv
. fprime-venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -r lib/fprime/requirements.txt
```

## 7) Build deployment natively on Pi

```bash
cd /home/pi/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
. fprime-venv/bin/activate
fprime-util generate -f
fprime-util build
```

## 8) Link current runtime paths

```bash
cd /home/pi/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
APP="$(find build-artifacts -type f -name ArtemisRpiTeensyDeployment | grep '/Linux/' | head -n 1)"
DICT="$(find build-artifacts -type f -name ArtemisRpiTeensyDeploymentTopologyDictionary.json | grep '/Linux/' | head -n 1)"
APP_ABS="$(readlink -f "$APP")"
DICT_ABS="$(readlink -f "$DICT")"
mkdir -p /home/pi/artemis/current /home/pi/artemis/logs
ln -sfn "$APP_ABS" /home/pi/artemis/current/ArtemisRpiTeensyDeployment
ln -sfn "$DICT_ABS" /home/pi/artemis/current/ArtemisRpiTeensyDeploymentTopologyDictionary.json
```

## 9) Smoke test (no UART dependency)

```bash
/home/pi/artemis/current/ArtemisRpiTeensyDeployment -d /dev/null
```

Stop with `Ctrl+C`.

## 10) Run against UART

```bash
/home/pi/artemis/current/ArtemisRpiTeensyDeployment -d /dev/serial0
```

## 11) Run in background with logs

```bash
nohup /home/pi/artemis/current/ArtemisRpiTeensyDeployment -d /dev/serial0 > /home/pi/artemis/logs/flight.log 2>&1 &
echo "PID: $!"
tail -n 50 /home/pi/artemis/logs/flight.log
```

