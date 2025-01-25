import socket

# Server configuration
SERVER_HOST = 'localhost'
SERVER_PORT = 53948  # Adjust to your server's port

# Test cases with expected responses
test_cases = [
    {
        "request": "GETFILE GET /courses/ud923/filecorpus/yellowstone.jpg\r\n\r\n",
        "expected": "GETFILE OK",
        "description": "Valid request with an existing file"
    },
    {
        "request": "GETFILE GET /nonexistent/file.txt\r\n\r\n",
        "expected": "GETFILE FILE_NOT_FOUND",
        "description": "Request for a non-existing file"
    },
    {
        "request": "GETFILE GET no/leading/slash\r\n\r\n",
        "expected": "GETFILE INVALID",
        "description": "Request without leading slash in path"
    },
    # {
    #     "request": "GETFILE GET /\r\n\r\n",
    #     "expected": "GETFILE OK",
    #     "description": "Request for root path"
    # },
    {
        "request": "INVALID REQUEST\r\n\r\n",
        "expected": "GETFILE INVALID",
        "description": "Malformed request"
    },
    {
        "request": "GETFILE GET /path/too/long/" + "a" * 500 + "\r\n\r\n",
        "expected": "GETFILE FILE_NOT_FOUND",
        "description": "Request for a non-existing file"
    },
    {
        "request": "GETFILEGET /valid/path\r\n\r\n",
        "expected": "GETFILE INVALID",
        "description": "Malformed request without space"
    },
    {
        "request": "GETFILE    GET /valid/path\r\n\r\n",
        "expected": "GETFILE INVALID",
        "description": "Malformed request with too many space"
    },
    {
        "request": "GETFILE GET      /valid/path\r\n\r\n",
        "expected": "GETFILE INVALID",
        "description": "Malformed request with too many space"
    }
]

def send_request(request):
    """Send a request to the server and return the response"""
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.connect((SERVER_HOST, SERVER_PORT))
            s.sendall(request.encode())
            response = s.recv(1024).decode()
            return response.strip()
    except Exception as e:
        return f"ERROR: {e}"

def run_tests():
    print("Starting server tests...\n")
    
    for test in test_cases:
        print(f"Running test: {test['description']}")
        response = send_request(test["request"])
        print(f"Request Sent: {repr(test['request'])}")
        print(f"Expected: {test['expected']}")
        print(f"Received: {response}")

        if response.startswith(test["expected"]):
            print("✅ Test PASSED\n")
        else:
            print("❌ Test FAILED\n")

if __name__ == "__main__":
    run_tests()
