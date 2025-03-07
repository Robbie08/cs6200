package main

import (
	"fmt"
	"log"
	"net/http"
	"sync"
)

var (
	statusCodes = []int{404, 403, 500, 502, 503} // List of HTTP responses to cycle through
	index       = 0
	mu          sync.Mutex // Ensures thread safety for concurrent requests
)

// Handler function for incoming requests
func testHandler(w http.ResponseWriter, r *http.Request) {
	mu.Lock()
	defer mu.Unlock()

	// Extract the requested file path
	filePath := r.URL.Path // Example: "/master/1kb-sample-file-0.png"

	// Get the next status code from the list (round-robin)
	statusCode := statusCodes[index]
	index = (index + 1) % len(statusCodes) // Cycle through status codes

	// Log the request and response
	fmt.Printf("Received request: %s %s -> Responding with HTTP %d\n", r.Method, filePath, statusCode)

	// Set HTTP response code and message
	w.WriteHeader(statusCode)
	w.Write([]byte(fmt.Sprintf("HTTP %d - %s\n", statusCode, http.StatusText(statusCode))))
}

func main() {
	http.HandleFunc("/", testHandler) // Handle all requests with testHandler

	port := 8080
	fmt.Printf("Starting test server on http://localhost:%d\n", port)

	// Start HTTP server
	err := http.ListenAndServe(fmt.Sprintf(":%d", port), nil)
	if err != nil {
		log.Fatalf("Failed to start server: %v", err)
	}
}
