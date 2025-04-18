#include <map>
#include <mutex>
#include <shared_mutex>
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

#include "proto-src/dfs-service.grpc.pb.h"
#include "src/dfslibx-call-data.h"
#include "src/dfslibx-service-runner.h"
#include "dfslib-shared-p2.h"
#include "dfslib-servernode-p2.h"

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
using dfs_service::LockRequest;
using dfs_service::LockResponse;

using dfs_service::DFSService;


//
// STUDENT INSTRUCTION:
//
// Change these "using" aliases to the specific
// message types you are using in your `dfs-service.proto` file
// to indicate a file request and a listing of files from the server
//
using FileRequestType = FileRequest;
using FileListResponseType = FileList;

extern dfs_log_level_e DFS_LOG_LEVEL;

//
// STUDENT INSTRUCTION:
//
// As with Part 1, the DFSServiceImpl is the implementation service for the rpc methods
// and message types you defined in your `dfs-service.proto` file.
//
// You may start with your Part 1 implementations of each service method.
//
// Elements to consider for Part 2:
//
// - How will you implement the write lock at the server level?
// - How will you keep track of which client has a write lock for a file?
//      - Note that we've provided a preset client_id in DFSClientNode that generates
//        a client id for you. You can pass that to the server to identify the current client.
// - How will you release the write lock?
// - How will you handle a store request for a client that doesn't have a write lock?
// - When matching files to determine similarity, you should use the `file_checksum` method we've provided.
//      - Both the client and server have a pre-made `crc_table` variable to speed things up.
//      - Use the `file_checksum` method to compare two files, similar to the following:
//
//          std::uint32_t server_crc = dfs_file_checksum(filepath, &this->crc_table);
//
//      - Hint: as the crc checksum is a simple integer, you can pass it around inside your message types.
//
class DFSServiceImpl final :
    public DFSService::WithAsyncMethod_CallbackList<DFSService::Service>,
        public DFSCallDataManager<FileRequestType , FileListResponseType> {

private:

    /** The runner service used to start the service and manage asynchronicity **/
    DFSServiceRunner<FileRequestType, FileListResponseType> runner;

    /** The mount path for the server **/
    std::string mount_path;

    /** Mutex for managing the queue requests **/
    std::mutex queue_mutex;

    /** The vector of queued tags used to manage asynchronous requests **/
    std::vector<QueueRequest<FileRequestType, FileListResponseType>> queued_tags;

    /** The mutex for our fileLocks map **/
    std::mutex fileLocksMtx;

    /** The fileLocks map that manages the files and their lock states **/
    std::map<std::string, std::string> fileLocks; 


    /**
     * Prepend the mount path to the filename.
     *
     * @param filepath
     * @return
     */
    const std::string WrapPath(const std::string &filepath) {
        return this->mount_path + filepath;
    }

    /** CRC Table kept in memory for faster calculations **/
    CRC::Table<std::uint32_t, 32> crc_table;

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

    DFSServiceImpl(const std::string& mount_path, const std::string& server_address, int num_async_threads):
        mount_path(mount_path), crc_table(CRC::CRC_32()) {

        this->runner.SetService(this);
        this->runner.SetAddress(server_address);
        this->runner.SetNumThreads(num_async_threads);
        this->runner.SetQueuedRequestsCallback([&]{ this->ProcessQueuedRequests(); });

    }

    ~DFSServiceImpl() {
        this->runner.Shutdown();
    }

    void Run() {
        this->runner.Run();
    }

    /**
     * Request callback for asynchronous requests
     *
     * This method is called by the DFSCallData class during
     * an asynchronous request call from the client.
     *
     * Students should not need to adjust this.
     *
     * @param context
     * @param request
     * @param response
     * @param cq
     * @param tag
     */
    void RequestCallback(grpc::ServerContext* context,
                         FileRequestType* request,
                         grpc::ServerAsyncResponseWriter<FileListResponseType>* response,
                         grpc::ServerCompletionQueue* cq,
                         void* tag) {

        std::lock_guard<std::mutex> lock(queue_mutex);
        this->queued_tags.emplace_back(context, request, response, cq, tag);

    }

    /**
     * Process a callback request
     *
     * This method is called by the DFSCallData class when
     * a requested callback can be processed. You should use this method
     * to manage the CallbackList RPC call and respond as needed.
     *
     * See the STUDENT INSTRUCTION for more details.
     *
     * @param context
     * @param request
     * @param response
     */
    void ProcessCallback(ServerContext* context, FileRequestType* request, FileListResponseType* response) {

        //
        // STUDENT INSTRUCTION:
        //
        // You should add your code here to respond to any CallbackList requests from a client.
        // This function is called each time an asynchronous request is made from the client.
        //
        // The client should receive a list of files or modifications that represent the changes this service
        // is aware of. The client will then need to make the appropriate calls based on those changes.
        //

    }

    /**
     * Processes the queued requests in the queue thread
     */
    void ProcessQueuedRequests() {
        while(true) {

            //
            // STUDENT INSTRUCTION:
            //
            // You should add any synchronization mechanisms you may need here in
            // addition to the queue management. For example, modified files checks.
            //
            // Note: you will need to leave the basic queue structure as-is, but you
            // may add any additional code you feel is necessary.
            //


            // Guarded section for queue
            {
                dfs_log(LL_DEBUG2) << "Waiting for queue guard";
                std::lock_guard<std::mutex> lock(queue_mutex);


                for(QueueRequest<FileRequestType, FileListResponseType>& queue_request : this->queued_tags) {
                    this->RequestCallbackList(queue_request.context, queue_request.request,
                        queue_request.response, queue_request.cq, queue_request.cq, queue_request.tag);
                    queue_request.finished = true;
                }

                // any finished tags first
                this->queued_tags.erase(std::remove_if(
                    this->queued_tags.begin(),
                    this->queued_tags.end(),
                    [](QueueRequest<FileRequestType, FileListResponseType>& queue_request) { return queue_request.finished; }
                ), this->queued_tags.end());

            }
        }
    }

    //
    // STUDENT INSTRUCTION:
    //
    // Add your additional code here, including
    // the implementations of your rpc protocol methods.
    //

    Status StoreFile(ServerContext* context, ServerReader<StoreFileChunk>* reader, GenericResponse* response) override {
        std::cout << "Received StoreFileRequest" << std::endl;

        StoreFileChunk chunk; // defined in my proto file
        std::ofstream ofs;    // output file stream
        std::string filePath; // This will be the full path where the server needs to write the file to
        std::string fileName; // The name of the file sent by client
        bool firstChunk = true; // Flag to check if this is the first chunk
        while (reader->Read(&chunk)) {
            // We only want to open the file once, when we receive the first chunk
            // If the fileName is empty, it means we are at the first chunk
            if (firstChunk) {
                fileName = chunk.name();
                if (fileName.empty()) {
                    dfs_log(LL_ERROR) << "Received empty file name";
                    return Status(StatusCode::CANCELLED, "Received empty file name");
                }

                if (!HasFileLock(chunk.name(), chunk.clientid())) {
                    dfs_log(LL_ERROR) << "LOCK access not granted to the current client. { file: " << chunk.name() << ", clientId: " << chunk.clientid() << "}";
                    return Status(StatusCode::RESOURCE_EXHAUSTED, "File is already locked by another client");
                }
    
                dfs_log(LL_SYSINFO) << "FileName: " << fileName;

                filePath = WrapPath(fileName); // Get the full path where the server needs to write the file to
                dfs_log(LL_SYSINFO) << "Destination file path: " << filePath;
                ofs.open(filePath, std::ios::binary);
                if (!ofs.is_open()) {
                    dfs_log(LL_ERROR) << "Failed to open file " << filePath << ". Error: " << strerror(errno);
                    ReleaseWriteLock(fileName); // Release the lock
                    return Status(StatusCode::CANCELLED, "Failed to open file for writing");
                }
                firstChunk = false; // We can set this to false since we are now processing the first chunk
            }

            // Write the chunk content to the file
            ofs.write(chunk.content().data(), chunk.content().size());
            if (ofs.fail()) {
                dfs_log(LL_ERROR) << "Failed to write to file " << fileName << ". Error: " << strerror(errno);
                ofs.close(); // Close the file before returning
                ReleaseWriteLock(fileName); // Release the lock
                return Status(StatusCode::CANCELLED, "Failed to write to file");
            }
            dfs_log(LL_DEBUG) << "Writing chunk of size: " << chunk.content().size() << " to file: " << filePath;
        }

        // We can close the file outside the critical section since we're not writing to it anymore
        // allows for other threads to enter the critical section while we're closing it
        ofs.close();
        ReleaseWriteLock(fileName); // Release the lock
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

    Status DeleteFile(ServerContext* context, const GenericRequest* request, GenericResponse* response) override {
        std::cout << "Received DeleteFileRequest" << std::endl;
        std::string fileName = request->name();
        response->set_name(fileName);
        if (fileName.empty()) {
            dfs_log(LL_ERROR) << "Received empty file name";
            return Status(StatusCode::CANCELLED, "Received empty file name");
        }

        if (!HasFileLock(fileName, request->clientid())) {
            dfs_log(LL_ERROR) << "LOCK access not granted to the current client. { file: " << fileName << ", clientId: " << request->clientid() << "}";
            return Status(StatusCode::RESOURCE_EXHAUSTED, "File is already locked by another client");
        }
        
        std::string filePath = WrapPath(fileName);

        static struct stat fStats;
        if (stat(filePath.c_str(), &fStats) != 0) {
            dfs_log(LL_ERROR) << "File not found: " << filePath;
            ReleaseWriteLock(fileName);
            return Status(StatusCode::NOT_FOUND, "File not found");
        }

        response->set_mtime(static_cast<int64_t>(fStats.st_mtime));
        response->set_ctime(static_cast<int64_t>(fStats.st_ctime));
        response->set_size(static_cast<int64_t>(fStats.st_size));
        
        if (remove(filePath.c_str()) != 0) {
            dfs_log(LL_ERROR) << "Failed to delete file: " << filePath << ". Error: " << strerror(errno);
            ReleaseWriteLock(fileName);
            return Status(StatusCode::CANCELLED, "Failed to delete file");
        }


        dfs_log(LL_SYSINFO) << "File deleted successfully: " << filePath;
        ReleaseWriteLock(fileName);
        return Status::OK;
    }

    Status AcquireWriteLock(ServerContext* context, const LockRequest *request, LockResponse *response) override {
        
        // High level overview of the method:
        // 1. Acquire mutex for the lock map
        // 2. If the lock isn't held at all, then grant it to the current client
        // 3. If the lock is taken
        //      a. if the current client is the owner, respond with success as true since we're making it idempotent
        //      b. else, respond with success as false

        std::lock_guard<std::mutex> lock(fileLocksMtx);
        
        // From request, get fileName and clientId references

        // prevent taking a lock on an empty fileName
        const std::string &fileName = request->filename(); 
        if (fileName.empty()) {
            response->set_success(false);
            response->set_message("fileName is empty");
            response->set_currentholder("");
            dfs_log(LL_ERROR) << "LOCK request failed because fileName is empty. { file: " << fileName << ", clientId: " << request->clientid() << "}";
            return Status::CANCELLED;
        }

        // prevent allowing empty clientId to lock file
        const std::string &clientId = request->clientid();
        if (clientId.empty()) {
            response->set_success(false);
            response->set_message("clientId is empty");
            response->set_currentholder("");
            dfs_log(LL_ERROR) << "LOCK request failed because clientId is empty. { file: " << fileName << ", clientId: " << clientId << "}";
            return Status::CANCELLED;
        }

        // To check if the file is already locked, we can just fetch the fileName from the map
        auto mapIter = fileLocks.find(fileName); // get the iterator, if not found then we'll get end()
        
        if (mapIter == fileLocks.end()) {
            // currently not locked, let's lock it and send response
            fileLocks[fileName] = clientId; // Write to the map that the client has acquired the lock
            response->set_success(true);
            response->set_message("Lock acquired");
            response->set_currentholder(clientId);
            dfs_log(LL_SYSINFO) << "LOCK acquired! { file: " << fileName << ", clientId: " << clientId << "}";
            
            return Status::OK;
        } 

        if (mapIter->second == clientId) {
            // This means that the current client is the holder of the lock
            response->set_success(true);
            response->set_message("You already hold the lock");
            response->set_currentholder(clientId);
            dfs_log(LL_SYSINFO) << "LOCK already held by the requesting client! { file: " << fileName << ", clientId: " << clientId << "}";
            return Status::OK;
        }

        // if we get here then the lock is held by another client
        response->set_success(false);
        response->set_message("Lock is held by another client");
        response->set_currentholder(mapIter->second);
        dfs_log(LL_SYSINFO) << "LOCK is held by another client!  { file: " << fileName << ", clientId: " << clientId << "}";
        return Status::RESOURCE_EXHAUSTED;
    }

    void ReleaseWriteLock(const std::string &fileName) {
        std::lock_guard<std::mutex> lock(fileLocksMtx);
        fileLocks.erase(fileName); // remove the file from the map
        dfs_log(LL_SYSINFO) << "LOCK released! { file: " << fileName << "}";
    }

    bool HasFileLock(const std::string &fileName, const std::string &clientId) {
        std::lock_guard<std::mutex> lock(fileLocksMtx);
        auto mapIter = fileLocks.find(fileName); // get the iterator, if not found then we'll get end()
        if (mapIter == fileLocks.end() || mapIter->second != clientId) {
            // This means that the current client is not the holder of the lock
            dfs_log(LL_ERROR) << "LOCK access not granted to the current client. { file: " << fileName << ", clientId: " << clientId << "}";
            return false;
        }
        return true;
    }

};

//
// STUDENT INSTRUCTION:
//
// The following three methods are part of the basic DFSServerNode
// structure. You may add additional methods or change these slightly
// to add additional startup/shutdown routines inside, but be aware that
// the basic structure should stay the same as the testing environment
// will be expected this structure.
//
/**
 * The main server node constructor
 *
 * @param mount_path
 */
DFSServerNode::DFSServerNode(const std::string &server_address,
        const std::string &mount_path,
        int num_async_threads,
        std::function<void()> callback) :
        server_address(server_address),
        mount_path(mount_path),
        num_async_threads(num_async_threads),
        grader_callback(callback) {}
/**
 * Server shutdown
 */
DFSServerNode::~DFSServerNode() noexcept {
    dfs_log(LL_SYSINFO) << "DFSServerNode shutting down";
}

/**
 * Start the DFSServerNode server
 */
void DFSServerNode::Start() {
    DFSServiceImpl service(this->mount_path, this->server_address, this->num_async_threads);


    dfs_log(LL_SYSINFO) << "DFSServerNode server listening on " << this->server_address;
    service.Run();
}

//
// STUDENT INSTRUCTION:
//
// Add your additional definitions here
//
