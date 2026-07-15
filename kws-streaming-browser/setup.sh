#!/bin/bash

set -e

echo "Installing system dependencies..."

sudo apt update

sudo apt install -y \
    build-essential \
    cmake \
    python3 \
    python3-pip \
    python3-venv \
    ffmpeg \
    libasound2-dev

echo "Creating virtual environment..."

python3 -m venv .venv

source .venv/bin/activate

echo "Upgrading pip..."

python3 -m pip install --upgrade pip

echo "Installing Python dependencies..."

pip install -r requirements.txt

echo ""
echo "====================================="
echo "Setup completed successfully!"
echo "====================================="
echo ""
echo "Activate the environment with:"
echo "source .venv/bin/activate"
