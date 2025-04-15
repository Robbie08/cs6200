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

    int64_t GetFileMTime(std::string &filePath) {
        struct stat fStats;
        if (stat(filePath.c_str(), &fStats) != 0) {
            return -1;
        }

        return static_cast<int64_t>(fStats.st_mtime);
    }

    int64_t GetFileCTime(std::string &filePath) {
        struct stat fStats;
        if (stat(filePath.c_str(), &fStats) != 0) {
            return -1;
        }

        return static_cast<int64_t>(fStats.st_ctime);
    }

    /**
     * This method gets both the modification time and the creation time of a file.
     * @param filePath a string containing the path to the file
     * @param mtime a pointer to an int64_t variable to store the modification time
     * @param ctime a pointer to an int64_t variable to store the creation time
     * @return true if the operation was successful, false otherwise
     */
    bool GetFileTimes(std::string &filePath, int64_t* mtime, int64_t* ctime) {
        struct stat fStats;
        if (stat(filePath.c_str(), &fStats) != 0) {
            return false;
        }

        if (mtime) {
            *mtime = static_cast<int64_t>(fStats.st_mtime);
        }
        
        if (ctime) {
            *ctime = static_cast<int64_t>(fStats.st_ctime);
        }

        return true;
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
        
        int64_t mtime = -1, ctime = -1;
        if (!GetFileTimes(filePath, &mtime, &ctime)) {
            dfs_log(LL_ERROR) << "Failed to get file times for file: " << filePath;
            return Status::OK; // We can still return a success status even if we fail to get the times, this should not be a fatal error
        }

        response->set_mtime(mtime);
        response->set_ctime(ctime);
        return Status::OK;
    }

    Status GetFile(ServerContext* context, const GenericRequest* request, ServerWriter<GetFileResponse>* writer) override {
        std::cout << "Received GetFileRequest" << std::endl;

        std::string fileName = request->name();
        if (fileName.empty()) {
            dfs_log(LL_ERROR) << "Received empty file name";
            return Status(StatusCode::CANCELLED, "Received empty file name");
        }

        dfs_log(LL_SYSINFO) << "FileName: " << fileName;
        std::string filePath = WrapPath(fileName);
        
        {
            std::lock_guard<std::mutex> lock(file_mtx); // take the mutex to protect the file access
            std::ifstream ifs(filePath, std::ios::binary);

            // Check if the file exists
            if (!ifs.is_open()) {
                dfs_log(LL_ERROR) << "File not found: " << filePath;
                return Status(StatusCode::NOT_FOUND, "File not found");
            }
            dfs_log(LL_SYSINFO) << "File found: " << filePath;
            
            StreamFileToClient(ifs, filePath, fileName, writer);
        } // release the mutex

        dfs_log(LL_SYSINFO) << "File sent successfully to client: " << filePath;
        return Status::OK;
    }

    Status DeleteFile(ServerContext* context, const GenericRequest* request, GenericResponse* response) override {
        std::cout << "Received DeleteFileRequest" << std::endl;
        std::string fileName = request->name();
        response->set_name(fileName);
        if (fileName.empty()) {
            dfs_log(LL_ERROR) << "Received empty file name";
            return Status(StatusCode::CANCELLED, "Received empty file name");
        }

        std::string filePath = WrapPath(fileName);

        {
            std::lock_guard<std::mutex> lock(file_mtx); // take the mutex to protect the file access

            static struct stat fStats;
            if (stat(filePath.c_str(), &fStats) != 0) {
                dfs_log(LL_ERROR) << "File not found: " << filePath;
                return Status(StatusCode::NOT_FOUND, "File not found");
            }

            response->set_mtime(static_cast<int64_t>(fStats.st_mtime));
            response->set_ctime(static_cast<int64_t>(fStats.st_ctime));
            response->set_size(static_cast<int64_t>(fStats.st_size));
            
            if (remove(filePath.c_str()) != 0) {
                dfs_log(LL_ERROR) << "Failed to delete file: " << filePath << ". Error: " << strerror(errno);
                return Status(StatusCode::CANCELLED, "Failed to delete file");
            }
        } // release the mutex

        dfs_log(LL_SYSINFO) << "File deleted successfully: " << filePath;
        return Status::OK;
    }

    Status GetAllFiles(ServerContext* context, const GetAllFilesRequest* request, GetAllFilesResponse* response) override {
        std::cout << "Received GetAllFilesRequest" << std::endl;

        {
            std::lock_guard<std::mutex> lock(file_mtx); // take the mutex to protect the file access
            // 1. Open directory containing the files
            std::string mntPath = this->mount_path;

            // Used some code from this reference: https://www.cppstories.com/2019/04/dir-iterate/ to learn how to iterate through a directory
            DIR* dir = nullptr;
            dir = opendir(mntPath.c_str());
            if (dir == nullptr) {
                dfs_log(LL_ERROR) << "Failed to open directory: " << mntPath;
                return Status(StatusCode::CANCELLED, "Failed to open directory");
            }

            struct dirent* entry = nullptr;
            while((entry = readdir(dir)) != nullptr) {
                if (entry->d_type != DT_REG) {
                    // basically we can skip everything that isn't a file
                    continue;
                }

                // 2. Read directory entiries and create a GenericResponse object adding to the list of files
                GenericResponse* entryResponse = response->add_files(); 
                entryResponse->set_name(entry->d_name); // Set the name of the file
                std::string filePath = WrapPath(entry->d_name);
                
                // 3. Populate that GenericResponse object with the file name, mtime, ctime, and size
                struct stat fStats;
                if (stat(filePath.c_str(), &fStats) != 0) {
                    dfs_log(LL_ERROR) << "Failed to get file status for file: " << filePath;
                    continue; // Skip this file if we can't get its status
                }
                entryResponse->set_mtime(static_cast<int64_t>(fStats.st_mtime)); // Set the mtime of the file
                entryResponse->set_ctime(static_cast<int64_t>(fStats.st_ctime)); // Set the ctime of the file
                entryResponse->set_size(static_cast<int64_t>(fStats.st_size)); // Set the size of the file
            }
            closedir(dir); // Close the directory
        } // release the mutex
        
        dfs_log(LL_SYSINFO) << "All files sent successfully";
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


Status StreamFileToClient(std::ifstream &ifs, const std::string &filePath, std::string &fileName, ServerWriter<GetFileResponse> *writer) {
    GetFileResponse response;
    const int chunkSize = 4096; // 4KB
    char buff[chunkSize];
    
    // We can start reading the file from disk, sending it to the client
    bool firstChunk = true;
    while (!ifs.eof()) {
        ifs.read(buff, chunkSize);
        std::streamsize bytesRead = ifs.gcount(); // If chunk(or file) is smaller than 4KB, this will ensure we don't read more than we have

        if (ifs.bad()) {
            dfs_log(LL_ERROR) << "Failed to read from file: " << filePath << ". Error: " << strerror(errno);
            return Status(StatusCode::CANCELLED, "Failed to read from file");
        }

        if (bytesRead <= 0) {
            break; // No more data to read
        }

        GetFileResponse chunk;
        if (firstChunk) {
            // We only want to send the mtime and name on the first chunk
            chunk.set_name(fileName);
            chunk.set_mtime(GetFileMTime(filePath)); 
            firstChunk = false;
        }

        chunk.set_content(buff, bytesRead); // Set the content of the chunk
        
        if (!writer->Write(chunk)) {
            dfs_log(LL_ERROR) << "Failed to write chunk to client for file " << fileName;
            return Status(StatusCode::CANCELLED, "Failed to write chunk to client");
        }
        dfs_log(LL_DEBUG) << "Sending chunk of size: " << bytesRead << " to client for file: " << filePath;
    }
    return Status::OK;
}