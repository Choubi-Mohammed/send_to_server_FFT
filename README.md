# FFT Data Processing and Server Project

This project consists of multiple components:
- A Python backend for data processing
- A Node.js backend for server functionality
- Arduino code for FFT implementation

## Project Structure

```
send_to_server_FFT/
├── backend-python/        # Python backend for data processing
│   └── venv/              # Python virtual environment with dependencies
├── back-end-nodejs/       # Node.js server implementation
│   ├── package.json       # Node.js dependencies
│   ├── package-lock.json  # Locked versions of dependencies
│   └── server.js          # Main server file
└── code_FFt/              # Arduino FFT implementation
    └── code_FFt.ino       # Arduino code
```

## Setup and Running Instructions

### Python Backend

1. The Python environment is already set up in the `backend-python/venv` directory.
2. To activate the virtual environment:

   **Windows:**
   ```
   backend-python\venv\Scripts\activate
   ```

   **Linux/Mac:**
   ```
   source backend-python/venv/Scripts/activate
   ```

3. To run the Python backend:
   ```
   python backend-python/venv/main.py
   ```

### Node.js Backend

1. Install Node.js dependencies:
   ```
   cd back-end-nodejs
   npm install
   ```

2. Run the Node.js server:
   ```
   node server.js
   ```

### Arduino Code

1. Open the Arduino IDE
2. Load the `code_FFt/code_FFt.ino` file
3. Connect your Arduino device
4. Upload the code to your Arduino

## Requirements

- Python 3.13.3 or compatible version
- Node.js (version compatible with the package.json)
- Arduino IDE (for the FFT implementation)

## Notes

- Make sure all services are running when using the complete system
- The Python backend processes data that can be sent to the Node.js server
- The Arduino code implements FFT (Fast Fourier Transform) functionality

## License

[Your license information here]
