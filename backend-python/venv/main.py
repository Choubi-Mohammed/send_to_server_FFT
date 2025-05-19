import os
import socket
import logging
from datetime import datetime
from flask import Flask, request, jsonify
from logging.handlers import RotatingFileHandler
import psutil  # For network info and system monitoring

app = Flask(__name__)

# Configuration
PORT = int(os.getenv('PORT', 3000))
LOG_DIR = 'logs'
os.makedirs(LOG_DIR, exist_ok=True)

# Configure logging
def setup_logging():
    # Main access log
    access_handler = RotatingFileHandler(
        os.path.join(LOG_DIR, 'access.log'),
        maxBytes=1024*1024,
        backupCount=5
    )
    access_handler.setLevel(logging.INFO)
    access_formatter = logging.Formatter(
        '%(asctime)s %(message)s',
        datefmt='%Y-%m-%dT%H:%M:%S'
    )
    access_handler.setFormatter(access_formatter)
    app.logger.addHandler(access_handler)
    
    # Error log
    error_handler = RotatingFileHandler(
        os.path.join(LOG_DIR, 'error.log'),
        maxBytes=1024*1024,
        backupCount=5
    )
    error_handler.setLevel(logging.ERROR)
    error_formatter = logging.Formatter(
        '%(asctime)s - Error: %(message)s\nStack: %(exc_info)s',
        datefmt='%Y-%m-%dT%H:%M:%S'
    )
    error_handler.setFormatter(error_formatter)
    app.logger.addHandler(error_handler)
    
    # Also log to console
    console_handler = logging.StreamHandler()
    console_handler.setLevel(logging.INFO)
    console_handler.setFormatter(access_formatter)
    app.logger.addHandler(console_handler)
    
    app.logger.setLevel(logging.INFO)

setup_logging()

# Middleware equivalent
@app.before_request
def log_request():
    if request.method == 'POST' and request.path == '/api/detections':
        request_log = f"{datetime.now().isoformat()} - Request from {request.remote_addr}:\nBody: {request.get_json()}\n"
        with open(os.path.join(LOG_DIR, 'requests.log'), 'a') as f:
            f.write(request_log)
        app.logger.info(f"Received detection: {request.get_json()}")

@app.after_request
def log_response(response):
    """Log all requests with similar format to morgan"""
    timestamp = datetime.now().isoformat()
    client_ip = request.headers.get('X-Forwarded-For', request.remote_addr)
    log_message = f"{timestamp} {client_ip} {request.method} {request.path} {response.status_code} - {response.content_length}"
    app.logger.info(log_message)
    return response

@app.errorhandler(Exception)
def handle_exception(e):
    """Log exceptions"""
    error_message = f"{datetime.now().isoformat()} - Error: {str(e)}\nStack: {e.__traceback__}\n"
    with open(os.path.join(LOG_DIR, 'error.log'), 'a') as f:
        f.write(error_message)
    app.logger.error(error_message)
    return jsonify(error=str(e)), 500

def get_network_info():
    """Get network information similar to Node.js version"""
    hostname = socket.gethostname()
    local_ip = socket.gethostbyname(hostname)
    interfaces = psutil.net_if_addrs()
    
    results = {
        "localhost": f"http://localhost:{PORT}",
        "networks": []
    }
    
    for interface, addrs in interfaces.items():
        for addr in addrs:
            if addr.family == socket.AF_INET and not addr.address.startswith('127.'):
                results["networks"].append({
                    "interface": interface,
                    "ip": addr.address,
                    "url": f"http://{addr.address}:{PORT}"
                })
    
    return results

# API Routes
@app.route('/api/detections', methods=['POST'])
def handle_detection():
    try:
        data = request.get_json()
        frequency = data.get('frequency')
        magnitude = data.get('magnitude')
        timestamp = data.get('timestamp', datetime.now().isoformat())
        
        if not frequency or not magnitude:
            error_msg = "Missing required fields"
            app.logger.error(f"Error processing request from {request.remote_addr}: {error_msg}")
            return jsonify({
                "error": error_msg,
                "required": ["frequency", "magnitude"]
            }), 400

        detection = {
            "frequency": frequency,
            "magnitude": magnitude,
            "timestamp": timestamp,
            "receivedAt": datetime.now().isoformat(),
            "clientIp": request.remote_addr
        }

        app.logger.info(f"Detection received - Frequency: {frequency} Hz, Magnitude: {magnitude}, Client: {request.remote_addr}")
        
        # Log to detections log
        detection_log = f"{datetime.now().isoformat()} - Detection: {detection}\n"
        with open(os.path.join(LOG_DIR, 'detections.log'), 'a') as f:
            f.write(detection_log)

        return jsonify({
            "success": True,
            "message": "Detection recorded",
            "detection": detection
        }), 201

    except Exception as e:
        app.logger.error(f"Error processing detection from {request.remote_addr}: {str(e)}")
        return jsonify({
            "error": "Internal server error",
            "details": str(e)
        }), 500

@app.route('/api/health', methods=['GET'])
def health_check():
    health_info = {
        "status": "healthy",
        "timestamp": datetime.now().isoformat(),
        "uptime": psutil.boot_time(),
        "memory": psutil.virtual_memory()._asdict(),
        "clientIp": request.remote_addr,
        "serverIp": get_network_info()
    }
    
    app.logger.info(f"Health check from {request.remote_addr}")
    return jsonify(health_info)

def print_startup_info():
    """Print colorful startup information similar to Node.js version"""
    network_info = get_network_info()
    
    print("\n\033[36m=== FFT Detection Server ===\033[0m")  # Cyan colored title
    print("\n\033[32mServer is running!\033[0m")
    print(f"\nLocal access: \033[33m{network_info['localhost']}\033[0m")
    
    if network_info['networks']:
        print("\nNetwork access:")
        for net in network_info['networks']:
            print(f"- {net['interface']}: \033[33m{net['url']}\033[0m")
    else:
        print("\n\033[31mNo network interfaces found!\033[0m")

    print("\nAvailable endpoints:")
    print("- POST \033[33m/api/detections\033[0m")
    print("- GET  \033[33m/api/health\033[0m")
    print("\nLogs directory:", os.path.abspath(LOG_DIR))
    print("\nPress CTRL+C to stop\n")

if __name__ == '__main__':
    print_startup_info()
    app.run(host='0.0.0.0', port=PORT)