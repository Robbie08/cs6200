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
using dfs_service::LockRequest;
using dfs_service::LockResponse;
using dfs_service::FileRequest;
using dfs_service::FileList;

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


    std::mutex file_mtx; // mutex for accessing file system

    std::mutex tombstone_mutex;
    std::vector<std::string> tombstones; // List of tombstones for deleted files


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

    int64_t GetFileMTime(const std::string &filePath) {
        struct stat fStats;
        if (stat(filePath.c_str(), &fStats) != 0) {
            return -1;
        }

        return static_cast<int64_t>(fStats.st_mtime);
    }

    int64_t GetFileCTime(const std::string &filePath) {
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
    bool GetFileTimes(const std::string &filePath, int64_t* mtime, int64_t* ctime) {
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

    Status StreamFileToClient(std::ifstream &ifs, const std::string &filePath, const std::string &fileName, ServerWriter<GetFileResponse> *writer) {
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

        // We need to basically do what we did for Part 1 GetAllFiles which is to get the list of 
        // files all the files from our mounted directory and their metadata.
        {
            dfs_log(LL_SYSINFO) << "Processing Callback: " << request->name();
            std::lock_guard<std::mutex> lock(file_mtx); // take the mutex to protect the file access
            // 1. Open directory containing the files
            std::string mntPath = this->mount_path;

            // Used some code from this reference: https://www.cppstories.com/2019/04/dir-iterate/ to learn how to iterate through a directory
            DIR* dir = nullptr;
            dir = opendir(mntPath.c_str());
            if (dir == nullptr) {
                dfs_log(LL_ERROR) << "Failed to open directory: " << mntPath;
                return;
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

        // since the heavy lifting is done in the Delete by keeping track of the tombstones
        // we can just send the list of tombstones to the client
        {
            std::lock_guard<std::mutex> tombstone_lock(tombstone_mutex);
            // for each file int the tombsones we must add it to the response
            for (const auto &fileName : tombstones) {
                response->add_tombstones(fileName);
            }
            tombstones.clear(); // empty the tombstones list after sending it to the client
        }

        dfs_log(LL_SYSINFO) << "Processed Callback: " << response->files_size() << "files, " << response->tombstones_size() << " tombstones";

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

        // We need to update the tombstone list so that our ProcessCallbacks method can send the updates to the clients
        {
            std::lock_guard<std::mutex> tombstone_lock(tombstone_mutex);
            tombstones.push_back(fileName);
        }

        dfs_log(LL_SYSINFO) << "File deleted successfully: " << filePath;
        ReleaseWriteLock(fileName);
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

            // Just to be safe we can close the file here instead of relying on the destructor
            // ifs.close();
            // if (ifs.fail()) {
            //     dfs_log(LL_ERROR) << "Failed to close file: " << fileName;
            //     return Status(StatusCode::CANCELLED, "Failed to close file");
            // }
        } // release the mutex

        dfs_log(LL_SYSINFO) << "File sent successfully to client: " << filePath;
        return Status::OK;
    }

    Status GetFileStatus(ServerContext* context, const GenericRequest* request, GenericResponse* response) override {
        std::cout << "Received GetFileStatusRequest" << std::endl;
        std::string filepath = WrapPath(request->name());
        struct stat fStats;
        if (stat(filepath.c_str(), &fStats) != 0) {
            dfs_log(LL_ERROR) << "Failed stat() the file: " << filepath;
            return Status(StatusCode::NOT_FOUND, "File not found");
        }

        response->set_name(request->name());
        response->set_mtime(static_cast<int64_t>(fStats.st_mtime));
        response->set_ctime(static_cast<int64_t>(fStats.st_ctime));
        response->set_size(static_cast<int64_t>(fStats.st_size));
        dfs_log(LL_SYSINFO) << "File status sent successfully: " << filepath;
        dfs_log(LL_SYSINFO) << "File details: mtime=" << response->mtime() << ", ctime=" << response->ctime() << ", size=" << response->size();
        return Status::OK;
    }

    Status ListAllFiles(ServerContext *context, const FileRequest *request, FileList *response) override {
        std::cout << "Received ListAllFiles Request" << std::endl;

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
        return Status(StatusCode::RESOURCE_EXHAUSTED, "Lock is held by another client");
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
