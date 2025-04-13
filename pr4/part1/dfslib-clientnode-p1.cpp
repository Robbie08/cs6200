#include <regex>
#include <vector>
#include <string>
#include <thread>
#include <cstdio>
#include <chrono>
#include <errno.h>
#include <csignal>
#include <iostream>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <getopt.h>
#include <unistd.h>
#include <limits.h>
#include <sys/inotify.h>
#include <grpcpp/grpcpp.h>

#include "dfslib-shared-p1.h"
#include "dfslib-clientnode-p1.h"
#include "proto-src/dfs-service.grpc.pb.h"

using grpc::Status;
using grpc::Channel;
using grpc::StatusCode;
using grpc::ClientWriter;
using grpc::ClientReader;
using grpc::ClientContext;

using dfs_service::StoreFileChunk;
using dfs_service::GenericRequest;
using dfs_service::GenericResponse;

//
// STUDENT INSTRUCTION:
//
// You may want to add aliases to your namespaced service methods here.
// All of the methods will be under the `dfs_service` namespace.
//
// For example, if you have a method named MyMethod, add
// the following:
//
//      using dfs_service::MyMethod
//


DFSClientNodeP1::DFSClientNodeP1() : DFSClientNode() {}

DFSClientNodeP1::~DFSClientNodeP1() noexcept {}

StatusCode DFSClientNodeP1::Store(const std::string &filename) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to store a file here. This method should
    // connect to your gRPC service implementation method
    // that can accept and store a file.
    //
    // When working with files in gRPC you'll need to stream
    // the file contents, so consider the use of gRPC's ClientWriter.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::NOT_FOUND - if the file cannot be found on the client
    // StatusCode::CANCELLED otherwise
    //

    
    std::string filepath = WrapPath(filename); // Get source path for the local file
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        dfs_log(LL_ERROR) << "Failed to open file " << filepath << ". Error: " << strerror(errno);
        return StatusCode::NOT_FOUND;
    }

    // NOw that the file is open, send the chunks to the server

    GenericResponse genericResponse;
    ClientContext context;

    // Setup the gRPC ClientWriter
    std::unique_ptr<grpc::ClientWriter<dfs_service::StoreFileChunk>> writer(
        service_stub->StoreFile(&context, &genericResponse));
    if (!writer) {
        dfs_log(LL_ERROR) << "Failed to create writer for file " << filepath;
        return StatusCode::CANCELLED;
    }

    const int chunkSize = 4096; // 4KB
    char buffer[chunkSize];
    bool firstChunk = true;

    // loop through the file reading chunk by chunk and sending it to the server
    while(!file.eof()) {
        // read a chunk of the file into the buffer
        file.read(buffer, chunkSize);
        std::streamsize bytesRead = file.gcount(); // We won't always read 4 KB (file smaller than 4KB and at end of file)
        if (bytesRead <= 0) {
            break; // No more data to read
        }

        // create and package up the chunk
        StoreFileChunk chunk;
        chunk.set_content(buffer, bytesRead);
        if (firstChunk) {
            chunk.set_name(filename); // This lets our server know the name of the file
            firstChunk = false;
        }

        // send the chunk to the server
        if (!writer->Write(chunk)) {
            dfs_log(LL_ERROR) << "Failed to write chunk to server for file " << filepath;
            return StatusCode::CANCELLED;
        }
    }

    writer->WritesDone(); // Indicate that we are done sending chunks
    Status status = writer->Finish(); // Finish the write operation
    file.close(); // Close the file)

    if (!status.ok()) {
        dfs_log(LL_ERROR) << "Failed to finish writing file " << filepath << ". Error: " << status.error_message();
        
        if (status.error_code() == StatusCode::DEADLINE_EXCEEDED) {
            return StatusCode::DEADLINE_EXCEEDED;
        } else if (status.error_code() == StatusCode::NOT_FOUND) {
            dfs_log(LL_ERROR) << "File not found on server for file " << filepath;
            return StatusCode::NOT_FOUND;
        } else {
            dfs_log(LL_ERROR) << "Operation cancelled for file " << filepath;
            return StatusCode::CANCELLED;
        }
    }
    
    // if (genericResponse.status() != dfs_service::OK) {
    //     dfs_log(LL_ERROR) << "Failed to store file " << filepath << ". Server response: " << genericResponse.status();
    //     return StatusCode::CANCELLED;
    // }

    dfs_log(LL_SYSINFO) << "Successfully stored file " << filepath;
    return StatusCode::OK; // Successfully stored the file
}


StatusCode DFSClientNodeP1::Fetch(const std::string &filename) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to fetch a file here. This method should
    // connect to your gRPC service implementation method
    // that can accept a file request and return the contents
    // of a file from the service.
    //
    // As with the store function, you'll need to stream the
    // contents, so consider the use of gRPC's ClientReader.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::NOT_FOUND - if the file cannot be found on the server
    // StatusCode::CANCELLED otherwise
    //
    //
}

StatusCode DFSClientNodeP1::Delete(const std::string& filename) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to delete a file here. Refer to the Part 1
    // student instruction for details on the basics.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::NOT_FOUND - if the file cannot be found on the server
    // StatusCode::CANCELLED otherwise
    //

}

StatusCode DFSClientNodeP1::List(std::map<std::string,int>* file_map, bool display) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to list all files here. This method
    // should connect to your service's list method and return
    // a list of files using the message type you created.
    //
    // The file_map parameter is a simple map of files. You should fill
    // the file_map with the list of files you receive with keys as the
    // file name and values as the modified time (mtime) of the file
    // received from the server.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::CANCELLED otherwise
    //
    //
}

StatusCode DFSClientNodeP1::Stat(const std::string &filename, void* file_status) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to get the status of a file here. This method should
    // retrieve the status of a file on the server. Note that you won't be
    // tested on this method, but you will most likely find that you need
    // a way to get the status of a file in order to synchronize later.
    //
    // The status might include data such as name, size, mtime, crc, etc.
    //
    // The file_status is left as a void* so that you can use it to pass
    // a message type that you defined. For instance, may want to use that message
    // type after calling Stat from another method.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::NOT_FOUND - if the file cannot be found on the server
    // StatusCode::CANCELLED otherwise
    //
    //
}

//
// STUDENT INSTRUCTION:
//
// Add your additional code here, including
// implementations of your client methods
//


