#include <map>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <errno.h>
#include <iostream>
#include <fstream>
#include <getopt.h>
#include <dirent.h>
#include <sys/stat.h>
#include <grpcpp/grpcpp.h>

#include "src/dfs-utils.h"
#include "dfslib-shared-p1.h"
#include "dfslib-servernode-p1.h"
#include "proto-src/dfs-service.grpc.pb.h"

using grpc::Status;
using grpc::Server;
using grpc::StatusCode;
using grpc::ServerReader;
using grpc::ServerWriter;
using grpc::ServerContext;
using grpc::ServerBuilder;

using dfs_service::StoreFileChunk;
using dfs_service::GenericRequest;
using dfs_service::GenericResponse;
using dfs_service::GetFileResponse;
using dfs_service::GetAllFilesRequest;
using dfs_service::GetAllFilesResponse;


using dfs_service::DFSService;


//
// STUDENT INSTRUCTION:
//
// DFSServiceImpl is the implementation service for the rpc methods
// and message types you defined in the `dfs-service.proto` file.
//
// You should add your definition overrides here for the specific
// methods that you created for your GRPC service protocol. The
// gRPC tutorial described in the readme is a good place to get started
// when trying to understand how to implement this class.
//
// The method signatures generated can be found in `proto-src/dfs-service.grpc.pb.h` file.
//
// Look for the following section:
//
//      class Service : public ::grpc::Service {
//
// The methods returning grpc::Status are the methods you'll want to override.
//
// In C++, you'll want to use the `override` directive as well. For example,
// if you have a service method named MyMethod that takes a MyMessageType
// and a ServerWriter, you'll want to override it similar to the following:
//
//      Status MyMethod(ServerContext* context,
//                      const MyMessageType* request,
//                      ServerWriter<MySegmentType> *writer) override {
//
//          /** code implementation here **/
//      }
//
class DFSServiceImpl final : public DFSService::Service {

private:

    /** The mount path for the server **/
    std::string mount_path;

    /** Mutex that protects our file access **/
    std::mutex file_mtx;

    /**
     * Prepend the mount path to the filename.
     *
     * @param filepath
     * @return
     */
    const std::string WrapPath(const std::string &filepath) {
        return this->mount_path + filepath;
    }


public:

    DFSServiceImpl(const std::string &mount_path): mount_path(mount_path) {
    }

    ~DFSServiceImpl() {}

    //
    // STUDENT INSTRUCTION:
    //
    // Add your additional code here, including
    // implementations of your protocol service methods
    //

    Status StoreFile(ServerContext* context, ServerReader<StoreFileChunk>* reader, GenericResponse* response) override {
        std::cout << "Received StoreFileRequest" << std::endl;

        StoreFileChunk chunk; // defined in my proto file
        std::ofstream ofs;    // output file stream
        std::string filePath; // This will be the full path where the server needs to write the file to
        std::string fileName; // The name of the file sent by client

        { // enter the critical section
            std::lock_guard<std::mutex> lock(file_mtx);
            while (reader->Read(&chunk)) {

                // We only want to open the file once, when we receive the first chunk
                // If the fileName is empty, it means we are at the first chunk
                if (filePath.empty()) {
                    fileName = chunk.name();
                    if (fileName.empty()) {
                        dfs_log(LL_ERROR) << "Received empty file name";
                        return Status(StatusCode::CANCELLED, "Received empty file name");
                    }
        
                    dfs_log(LL_SYSINFO) << "FileName: " << fileName;

                    filePath = WrapPath(fileName); // Get the full path where the server needs to write the file to
                    dfs_log(LL_SYSINFO) << "Destination file path: " << filePath;
                    ofs.open(filePath, std::ios::binary);
                    if (!ofs.is_open()) {
                        dfs_log(LL_ERROR) << "Failed to open file " << filePath << ". Error: " << strerror(errno);
                        return Status(StatusCode::CANCELLED, "Failed to open file for writing");
                    }
                }

                // Write the chunk content to the file
                ofs.write(chunk.content().data(), chunk.content().size());
                if (ofs.fail()) {
                    dfs_log(LL_ERROR) << "Failed to write to file " << fileName << ". Error: " << strerror(errno);
                    ofs.close(); // Close the file before returning
                    return Status(StatusCode::CANCELLED, "Failed to write to file");
                }
                dfs_log(LL_DEBUG) << "Writing chunk of size: " << chunk.content().size() << " to file: " << filePath;
            }
        } // release the mutex

        // We can close the file outside the critical section since we're not writing to it anymore
        // allows for other threads to enter the critical section while we're closing it
        ofs.close();
        if (ofs.fail()) {
            dfs_log(LL_ERROR) << "Failed to write to file: " << fileName;
            return Status(StatusCode::CANCELLED, "Failed to write to file");
        }
        dfs_log(LL_SYSINFO) << "File written successfully at: " << filePath;
        response->set_name(fileName);
        return Status::OK;
    }

    Status GetFile(ServerContext* context, const GenericRequest* request, GetFileResponse* response) override {
        std::cout << "Received GetFileRequest" << std::endl;
        std::string filepath = WrapPath(request->name());
        return Status::OK;
    }

    Status DeleteFile(ServerContext* context, const GenericRequest* request, GenericResponse* response) override {
        std::cout << "Received DeleteFileRequest" << std::endl;
        std::string filepath = WrapPath(request->name());
        return Status::OK;
    }

    Status GetAllFiles(ServerContext* context, const GetAllFilesRequest* request, GetAllFilesResponse* response) override {
        std::cout << "Received GetAllFilesRequest" << std::endl;
        return Status::OK;
    }

    Status GetFileStatus(ServerContext* context, const GenericRequest* request, GenericResponse* response) override {
        std::cout << "Received GetFileStatusRequest" << std::endl;
        std::string filepath = WrapPath(request->name());
        return Status::OK;
    }


};

//
// STUDENT INSTRUCTION:
//
// The following three methods are part of the basic DFSServerNode
// structure. You may add additional methods or change these slightly,
// but be aware that the testing environment is expecting these three
// methods as-is.
//
/**
 * The main server node constructor
 *
 * @param server_address
 * @param mount_path
 */
DFSServerNode::DFSServerNode(const std::string &server_address,
        const std::string &mount_path,
        std::function<void()> callback) :
    server_address(server_address), mount_path(mount_path), grader_callback(callback) {}

/**
 * Server shutdown
 */
DFSServerNode::~DFSServerNode() noexcept {
    dfs_log(LL_SYSINFO) << "DFSServerNode shutting down";
    this->server->Shutdown();
}

/** Server start **/
void DFSServerNode::Start() {
    DFSServiceImpl service(this->mount_path);
    ServerBuilder builder;
    builder.AddListeningPort(this->server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    this->server = builder.BuildAndStart();
    dfs_log(LL_SYSINFO) << "DFSServerNode server listening on " << this->server_address;
    this->server->Wait();
}

//
// STUDENT INSTRUCTION:
//
// Add your additional DFSServerNode definitions here
//