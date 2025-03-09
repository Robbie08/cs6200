package main

import (
	"fmt"
	"log"
	"net/http"
	"sync"
)

var (
	statusCodes = []int{400, 401, 404, 403, 500, 502, 503}
	index       = 0
	mu          sync.Mutex // we need this to access index safely
)

func testHandler(w http.ResponseWriter, r *http.Request) {
	mu.Lock()
	defer mu.Unlock()

	// Get the next status code from the list (round-robin)
	statusCode := statusCodes[index]
	index = (index + 1) % len(statusCodes)

	// Extract the requested file path
	filePath := r.URL.Path
	fmt.Printf("Request: %s %s ---> Response: HTTP %d\n", r.Method, filePath, statusCode)

	// Send header and body
	w.WriteHeader(statusCode)
	w.Write([]byte(fmt.Sprintf("HTTP %d - %s\n", statusCode, http.StatusText(statusCode))))
}

func main() {
	http.HandleFunc("/", testHandler) // Handle all requests with testHandler

	port := 8080
	fmt.Printf("Starting test server on http://localhost:%d\n", port)

	err := http.ListenAndServe(fmt.Sprintf(":%d", port), nil)
	if err != nil {
		log.Fatalf("Failed to start server: %v", err)
	}
}
