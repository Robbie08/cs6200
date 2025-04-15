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
using dfs_service::GetFileResponse;
using dfs_service::GetAllFilesRequest;
using dfs_service::GetAllFilesResponse;

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

// Function prototypes
grpc::StatusCode HandleBadRPCStatus(const grpc::Status &status, const std::string &filepath);

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
    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(this->deadline_timeout)); // Add timeout to the context

    GenericResponse genericResponse;

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

        // Let's get rid of the file so we don't leave any corrupted files in the directory
        if (std::remove(filepath.c_str()) != 0) {
            dfs_log(LL_ERROR) << "Failed to remove file " << filepath << ". Error: " << strerror(errno);
        }
        
    }
    
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

    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(this->deadline_timeout)); // Add timeout to the context

    GenericRequest request;
    request.set_name(filename); // Set the name of the file to fetch

    std::unique_ptr<ClientReader<GetFileResponse>> reader = service_stub->GetFile(&context, request);
    if (!reader) {
        dfs_log(LL_ERROR) << "Failed to create reader for file " << filename;
        return StatusCode::CANCELLED;
    }

    std::string destPath = WrapPath(filename); // Get the destination path for the file
    std::ofstream ofs(destPath, std::ios::binary);
    if (!ofs.is_open()) {
        dfs_log(LL_ERROR) << "Failed to open file " << destPath << ". Error: " << strerror(errno);
        return StatusCode::CANCELLED;
    }

    GetFileResponse response; // This response will receive the stream of data for the file that was requested

    while(reader->Read(&response)) {
        ofs.write(response.content().data(), response.content().size());
        if (ofs.bad()) {
            dfs_log(LL_ERROR) << "Write failed for file " << destPath << ". Error: " << strerror(errno);
            ofs.close(); // Close the file before returning
            return StatusCode::CANCELLED;
        }

        // Fist chunk will send the mtiome and name information in case it needs it
    }

    // Read the status and check for errors during final transmission
    Status status = reader->Finish();
    ofs.close(); // Close the file

    if (!status.ok()) {
        
        // Let's get rid of the file so we don't leave any corrupted files in the directory
        if (std::remove(destPath.c_str()) != 0) {
            dfs_log(LL_ERROR) << "Failed to remove file " << destPath << ". Error: " << strerror(errno);
        }

        return HandleBadRPCStatus(status, filename);
    }

    dfs_log(LL_SYSINFO) << "Successfully fetched file " << filename;
    return StatusCode::OK; // Successfully fetched the file
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

    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(this->deadline_timeout)); // Add timeout to the context

    GenericRequest request;
    request.set_name(filename); // Set the name of the file to delete

    GenericResponse response;
    
    Status status = service_stub->DeleteFile(&context, request, &response);
    if (!status.ok()) {
        return HandleBadRPCStatus(status, filename);
    }


    dfs_log(LL_SYSINFO) << "Successfully deleted file " << filename;
    dfs_log(LL_SYSINFO) << "File details: mtime=" << response.mtime() << ", ctime=" << response.ctime() << ", size=" << response.size();
    return StatusCode::OK; // Successfully deleted the file
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


    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(this->deadline_timeout)); // Add timeout to the context

    GetAllFilesRequest request;
    GetAllFilesResponse response;

    Status status = service_stub->GetAllFiles(&context, request, &response);
    if (!status.ok()) {
        return HandleBadRPCStatus(status, "");
    }

    // loop through results from response and add them to the map
    // key is the file name and value is the mtime
    for (const auto& file : response.files()) {
        std::string name = file.name();
        int64_t mtime = file.mtime();
        (*file_map)[name] = static_cast<int>(mtime); // Fill the map with the file name and its mtime
        if (display) {
            dfs_log(LL_SYSINFO) << "File: " << name << ", mtime: " << mtime;
        }
    }

    return StatusCode::OK;
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
    return Status::OK;
}

//
// STUDENT INSTRUCTION:
//
// Add your additional code here, including
// implementations of your client methods
//

grpc::StatusCode HandleBadRPCStatus(const grpc::Status &status, const std::string &filepath) {
    if (status.error_code() == StatusCode::DEADLINE_EXCEEDED) {
        if (filepath.empty()) {
            dfs_log(LL_ERROR) << "Deadline exceeded for operation";
        } else {
            dfs_log(LL_ERROR) << "Deadline exceeded for file " << filepath;
        }
        
        return StatusCode::DEADLINE_EXCEEDED;
    } else if (status.error_code() == StatusCode::NOT_FOUND) {
        if (filepath.empty()) {
            dfs_log(LL_ERROR) << "A file was not found on server for file";
        } else {
            dfs_log(LL_ERROR) << "File not found on server for file " << filepath;
        }

        return StatusCode::NOT_FOUND;
    } else {
        if (filepath.empty()) {
            dfs_log(LL_ERROR) << "Operation cancelled";
        } else {
            dfs_log(LL_ERROR) << "Operation cancelled for file " << filepath;
        }
        return StatusCode::CANCELLED;
    }
}
