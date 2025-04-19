#include <regex>
#include <mutex>
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
#include <utime.h>
#include <dirent.h>
#include <sys/stat.h>

#include "src/dfs-utils.h"
#include "src/dfslibx-clientnode-p2.h"
#include "dfslib-shared-p2.h"
#include "dfslib-clientnode-p2.h"
#include "proto-src/dfs-service.grpc.pb.h"

using grpc::Status;
using grpc::Channel;
using grpc::StatusCode;
using grpc::ClientWriter;
using grpc::ClientReader;
using grpc::ClientContext;

extern dfs_log_level_e DFS_LOG_LEVEL;

//
// STUDENT INSTRUCTION:
//
// Change these "using" aliases to the specific
// message types you are using to indicate
// a file request and a listing of files from the server.
//

using dfs_service::LockRequest;
using dfs_service::LockResponse;
using dfs_service::GenericRequest;
using dfs_service::GenericResponse;
using dfs_service::GetFileResponse;
using dfs_service::StoreFileChunk;
using dfs_service::FileRequest;
using dfs_service::FileList;
using FileRequestType = FileRequest;
using FileListResponseType = FileList;

std::mutex callback_mutex; // I'll use this in the HandleCallback function

DFSClientNodeP2::DFSClientNodeP2() : DFSClientNode() {}
DFSClientNodeP2::~DFSClientNodeP2() {}

grpc::StatusCode DFSClientNodeP2::RequestWriteAccess(const std::string &filename) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to obtain a write lock here when trying to store a file.
    // This method should request a write lock for the given file at the server,
    // so that the current client becomes the sole creator/writer. If the server
    // responds with a RESOURCE_EXHAUSTED response, the client should cancel
    // the current file storage
    //
    // The StatusCode response should be:
    //
    // OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::RESOURCE_EXHAUSTED - if a write lock cannot be obtained
    // StatusCode::CANCELLED otherwise
    //
    //

    LockResponse response;
    ClientContext context;
    LockRequest request;

    request.set_filename(filename);
    request.set_clientid(this->client_id);

    // Set the deadline for the request
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(this->deadline_timeout)); // Add timeout to the context
    Status status = service_stub->AcquireWriteLock(&context, request, &response);
    
    if (!status.ok()) {
        // Scenarios are DEADLINE_EXCEEDED, RESOURCE_EXHAUSTED, CANCELLED
        if (status.error_code() == StatusCode::DEADLINE_EXCEEDED) {
            // timeout
            dfs_log(LL_ERROR) << "Deadline exceeded for write lock request";
            return StatusCode::DEADLINE_EXCEEDED; 
        } else if (status.error_code() == StatusCode::RESOURCE_EXHAUSTED) {
            // failed to obtain lock
            dfs_log(LL_ERROR) << "Failed to obtain write lock for file " << filename << ". Current holder: " << response.currentholder();
            return StatusCode::RESOURCE_EXHAUSTED;
        } else {
            // other errors
            dfs_log(LL_ERROR) << "Failed to obtain write lock for file " << filename << ". Error: " << response.message();
            return StatusCode::CANCELLED;
        }
    }

    // Let's just peform a sanity check here. Even though status is Ok, let's corroborate with our resposne sturcture
    if (!response.success()) {
        dfs_log(LL_ERROR) << "Failed to obtain write lock for file " << filename << ". Error: " << response.message() << "Current hoklder: " << response.currentholder();
        return StatusCode::RESOURCE_EXHAUSTED;
    }

    dfs_log(LL_SYSINFO) << "Successfully obtained write lock for file " << filename << ". Current holder: " << response.currentholder();
    return StatusCode::OK; // Successfully obtained the write lock
}

grpc::StatusCode DFSClientNodeP2::Store(const std::string &filename) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to store a file here. Refer to the Part 1
    // student instruction for details on the basics.
    //
    // You can start with your Part 1 implementation. However, you will
    // need to adjust this method to recognize when a file trying to be
    // stored is the same on the server (i.e. the ALREADY_EXISTS gRPC response).
    //
    // You will also need to add a request for a write lock before attempting to store.
    //
    // If the write lock request fails, you should return a status of RESOURCE_EXHAUSTED
    // and cancel the current operation.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::ALREADY_EXISTS - if the local cached file has not changed from the server version
    // StatusCode::RESOURCE_EXHAUSTED - if a write lock cannot be obtained
    // StatusCode::CANCELLED otherwise
    //
    //

    // Check if we have the file in the local mount
    std::string filepath = WrapPath(filename); // Get source path for the local file
    struct stat localStat;
    if (stat(filepath.c_str(), &localStat) != 0) {
        dfs_log(LL_ERROR) << "Failed to stat() the file: " << filepath << ". Error: " << strerror(errno);
        return StatusCode::NOT_FOUND;
    }

    // Request if the server has the file, then only send file if the local one is newer
    // If the file doesn't exist in the server, then perfect, we can send it the file
    GenericResponse statResponse;
    StatusCode statStatus = Stat(filename, &statResponse);
    if (statStatus == StatusCode::CANCELLED || statStatus == StatusCode::DEADLINE_EXCEEDED) {
        dfs_log(LL_ERROR) << "Error while fetching file stats on server for file: " << filename << ". Error: " << statStatus;
        return statStatus;
    } else if (statStatus == StatusCode::OK) {
        int64_t localMTime = static_cast<int64_t>(localStat.st_mtime);
        int64_t serverMTime = statResponse.mtime();

        if (localMTime <= serverMTime) {
            dfs_log(LL_SYSINFO) << "File " << filename << " already exists and is up to date. No need to send file.";
            return StatusCode::ALREADY_EXISTS; // File already exists and is up to date
        } else {
            dfs_log(LL_SYSINFO) << "File " << filename << " exists but is out of date. Sending new version to server.";
        }
    }

    // If we don't get the lock then we should exit immedietely
    StatusCode lockStatus = RequestWriteAccess(filename);
    if (lockStatus != StatusCode::OK) {
        dfs_log(LL_ERROR) << "Failed to obtain write lock for file " << filename << ". Error: " << lockStatus;
        return lockStatus; // Failed to obtain write lock
    }

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
            chunk.set_clientid(this->client_id);
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
    }
    
    dfs_log(LL_SYSINFO) << "Successfully stored file " << filepath;
    return StatusCode::OK; // Successfully stored the file

}


grpc::StatusCode DFSClientNodeP2::Fetch(const std::string &filename) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to fetch a file here. Refer to the Part 1
    // student instruction for details on the basics.
    //
    // You can start with your Part 1 implementation. However, you will
    // need to adjust this method to recognize when a file trying to be
    // fetched is the same on the client (i.e. the files do not differ
    // between the client and server and a fetch would be unnecessary.
    //
    // The StatusCode response should be:
    //
    // OK - if all went well
    // DEADLINE_EXCEEDED - if the deadline timeout occurs
    // NOT_FOUND - if the file cannot be found on the server
    // ALREADY_EXISTS - if the local cached file has not changed from the server version
    // CANCELLED otherwise
    //
    // Hint: You may want to match the mtime on local files to the server's mtime
    //

    GenericResponse statResponse;
    StatusCode statStatus = Stat(filename, &statResponse);
    if (statStatus != StatusCode::OK) {
        return statStatus; // If the status is not OK, return the status (DEADLINE_EXCEEDED, NOT_FOUND, CANCELLED)
    }

    // Check if we have the file in the local mount
    std::string destPath = WrapPath(filename); // Get the destination path for the file
    struct stat localStat;
    if (stat(destPath.c_str(), &localStat) == 0) {
        int64_t localMTime = static_cast<int64_t>(localStat.st_mtime);
        int64_t serverMTime = statResponse.mtime();

        // TODO: Come back to this... we might want to handle this differently if the current file is newer
        //       then then server file (e.g. localMTime > serverMTime)
        if (localMTime >= serverMTime) {
            dfs_log(LL_SYSINFO) << "File " << filename << " already exists and is up to date. No need to fetch.";
            return StatusCode::ALREADY_EXISTS; // File already exists and is up to date
        } else {
            dfs_log(LL_SYSINFO) << "File " << filename << " exists but is out of date. Fetching new version.";
        }
    }

    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(this->deadline_timeout)); // Add timeout to the context

    GenericRequest request;
    request.set_name(filename); // Set the name of the file to fetch
    request.set_clientid(this->client_id);
    
    std::unique_ptr<ClientReader<GetFileResponse>> reader = service_stub->GetFile(&context, request);
    if (!reader) {
        dfs_log(LL_ERROR) << "Failed to create reader for file " << filename;
        return StatusCode::CANCELLED;
    }

    
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

    // Since we updated the file then we should update the mtimes
    struct utimbuf newTimes;
    newTimes.actime = localStat.st_atime;
    newTimes.modtime = static_cast<time_t>(statResponse.mtime());
    if (utime(destPath.c_str(), &newTimes) != 0) {
        dfs_log(LL_ERROR) << "Failed to update file times for " << destPath << ". Error: " << strerror(errno);
    }

    dfs_log(LL_SYSINFO) << "File times updated for " << destPath << ". Access time: " << newTimes.actime << ", Modification time: " << newTimes.modtime;
    dfs_log(LL_SYSINFO) << "Successfully fetched file " << filename;
    return StatusCode::OK; // Successfully fetched the file
}

grpc::StatusCode DFSClientNodeP2::Delete(const std::string &filename) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to delete a file here. Refer to the Part 1
    // student instruction for details on the basics.
    //
    // You will also need to add a request for a write lock before attempting to delete.
    //
    // If the write lock request fails, you should return a status of RESOURCE_EXHAUSTED
    // and cancel the current operation.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::RESOURCE_EXHAUSTED - if a write lock cannot be obtained
    // StatusCode::CANCELLED otherwise
    //
    //


    // Since the server is the source of truth for this file system, then even if we don't have the
    // file locally we should still be able to delete it from the server. 
    StatusCode lockStatus = RequestWriteAccess(filename);
    if (lockStatus != StatusCode::OK) {
        dfs_log(LL_ERROR) << "Failed to obtain write lock for file " << filename << ". Error: " << lockStatus;
        return lockStatus; 
    }

    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(this->deadline_timeout)); // Add timeout to the context

    GenericRequest request;
    request.set_name(filename); // Set the name of the file to delete
    request.set_clientid(this->client_id);
    GenericResponse response;
    
    Status status = service_stub->DeleteFile(&context, request, &response);
    if (!status.ok()) {
        StatusCode handledStatus = HandleBadRPCStatus(status, filename);
        // We can proceed to delete the file even if the server doesn't have it
        if (handledStatus != StatusCode::NOT_FOUND) {
            return handledStatus;
        }
    }

    // If we deleted the file on the server or the server doesn't have the file then delete it locally
    std::string destPath = WrapPath(filename); // Get the destination path for the file
    struct stat localStat;
    if (stat(destPath.c_str(), &localStat) == 0) {
        dfs_log(LL_SYSINFO) << "File " << filename << " exists in local mount. Deleting it.";
        if (std::remove(destPath.c_str()) != 0) {
            dfs_log(LL_ERROR) << "Failed to remove file " << destPath << ". Error: " << strerror(errno);
            return StatusCode::CANCELLED;
        }
    } else {
        dfs_log(LL_SYSINFO) << "File " << filename << " does not exist in local mount. Proceeding to delete on server.";
    }


    dfs_log(LL_SYSINFO) << "Successfully deleted file " << filename;
    dfs_log(LL_SYSINFO) << "File details: mtime=" << response.mtime() << ", ctime=" << response.ctime() << ", size=" << response.size();
    return StatusCode::OK; 
}

grpc::StatusCode DFSClientNodeP2::List(std::map<std::string,int>* file_map, bool display) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to list files here. Refer to the Part 1
    // student instruction for details on the basics.
    //
    // You can start with your Part 1 implementation and add any additional
    // listing details that would be useful to your solution to the list response.
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

    FileRequest request;
    FileList response;
    Status status = service_stub->ListAllFiles(&context, request, &response);
    if (!status.ok()) {
        return HandleBadRPCStatus(status, ""); // No filename to pass here
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

grpc::StatusCode DFSClientNodeP2::Stat(const std::string &filename, void* file_status) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to get the status of a file here. Refer to the Part 1
    // student instruction for details on the basics.
    //
    // You can start with your Part 1 implementation and add any additional
    // status details that would be useful to your solution.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::NOT_FOUND - if the file cannot be found on the server
    // StatusCode::CANCELLED otherwise
    //
    //
    GenericRequest request;
    request.set_name(filename);

    GenericResponse response;
    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(this->deadline_timeout)); // Add timeout to the context
    Status status = service_stub->GetFileStatus(&context, request, &response);
    if (!status.ok()) {
        if (file_status) {
            GenericResponse* fileStatus = static_cast<GenericResponse*>(file_status);
            if (fileStatus) {
                *fileStatus = response;
            }
        }
        return HandleBadRPCStatus(status, filename);
    }

    dfs_log(LL_SYSINFO) << "File status: " << filename;
    dfs_log(LL_SYSINFO) << "File details: mtime=" << response.mtime() << ", ctime=" << response.ctime() << ", size=" << response.size();
    if (file_status) {
        GenericResponse* fileStatus = static_cast<GenericResponse*>(file_status);
        if (fileStatus) {
            *fileStatus = response;
        }
    }
    return StatusCode::OK;
}

void DFSClientNodeP2::InotifyWatcherCallback(std::function<void()> callback) {

    //
    // STUDENT INSTRUCTION:
    //
    // This method gets called each time inotify signals a change
    // to a file on the file system. That is every time a file is
    // modified or created.
    //
    // You may want to consider how this section will affect
    // concurrent actions between the inotify watcher and the
    // asynchronous callbacks associated with the server.
    //
    // The callback method shown must be called here, but you may surround it with
    // whatever structures you feel are necessary to ensure proper coordination
    // between the async and watcher threads.
    //
    // Hint: how can you prevent race conditions between this thread and
    // the async thread when a file event has been signaled?
    //

    std::lock_guard<std::mutex> callback_lock(callback_mutex);
    callback();

}

//
// STUDENT INSTRUCTION:
//
// This method handles the gRPC asynchronous callbacks from the server.
// We've provided the base structure for you, but you should review
// the hints provided in the STUDENT INSTRUCTION sections below
// in order to complete this method.
//
void DFSClientNodeP2::HandleCallbackList() {

    void* tag;

    bool ok = false;

    //
    // STUDENT INSTRUCTION:
    //
    // Add your file list synchronization code here.
    //
    // When the server responds to an asynchronous request for the CallbackList,
    // this method is called. You should then synchronize the
    // files between the server and the client based on the goals
    // described in the readme.
    //
    // In addition to synchronizing the files, you'll also need to ensure
    // that the async thread and the file watcher thread are cooperating. These
    // two threads could easily get into a race condition where both are trying
    // to write or fetch over top of each other. So, you'll need to determine
    // what type of locking/guarding is necessary to ensure the threads are
    // properly coordinated.
    //

    // Block until the next result is available in the completion queue.
    while (completion_queue.Next(&tag, &ok)) {
        {
            //
            // STUDENT INSTRUCTION:
            //
            // Consider adding a critical section or RAII style lock here
            //

            // The tag is the memory location of the call_data object
            AsyncClientData<FileListResponseType> *call_data = static_cast<AsyncClientData<FileListResponseType> *>(tag);

            dfs_log(LL_DEBUG2) << "Received completion queue callback";

            // Verify that the request was completed successfully. Note that "ok"
            // corresponds solely to the request for updates introduced by Finish().
            // GPR_ASSERT(ok);
            if (!ok) {
                dfs_log(LL_ERROR) << "Completion queue callback not ok.";
            }

            if (ok && call_data->status.ok()) {

                dfs_log(LL_DEBUG3) << "Handling async callback ";
                std::lock_guard<std::mutex> callback_lock(callback_mutex);

                //
                // STUDENT INSTRUCTION:
                //
                // Add your handling of the asynchronous event calls here.
                // For example, based on the file listing returned from the server,
                // how should the client respond to this updated information?
                // Should it retrieve an updated version of the file?
                // Send an update to the server?
                // Do nothing?
                //

                // 1. Get the list of files from the server (possibly conver it to a map for quick look ups?)
                std::unordered_map<std::string, GenericResponse> server_map;
                init_server_map(call_data->reply, server_map);

                // 2. Get List of files from local mount (possibly add it to a map for quick look ups?)
                std::unordered_map<std::string, struct stat> local_map;
                init_local_map(this->mount_path, local_map);

                // 3. Compare the sever files with the local files (going through each file in the server map)
                //    a. If the file is missing locally then fetch it from server
                //    b. If file exists in both:
                //       i. Fetch() if the file from server is newer
                //       ii. Store() if the file from server is older 
                //       iii. Don't care if the files are the same, move on
                
                compare_server_to_local(server_map, local_map);

                // 4. Compare local files to server (going through each item in the local map)
                //    No need to perform check if the file exists on both since we already did that
                //    a. If the file is missing then Store()
                
                compare_local_to_server(local_map, server_map);

                // 5. Go through the tombstones list and delete them locally
                const auto& proto_tombstones = call_data->reply.tombstones();
                std::vector<std::string> tombstone_vec(proto_tombstones.begin(), proto_tombstones.end());
                process_tombstones(tombstone_vec, this->mount_path);

                // relase lock
            } else {
                dfs_log(LL_ERROR) << "Status was not ok. Will try again in " << DFS_RESET_TIMEOUT << " milliseconds.";
                dfs_log(LL_ERROR) << call_data->status.error_message();
                std::this_thread::sleep_for(std::chrono::milliseconds(DFS_RESET_TIMEOUT));
            }

            // Once we're complete, deallocate the call_data object.
            delete call_data;

            //
            // STUDENT INSTRUCTION:
            //
            // Add any additional syncing/locking mechanisms you may need here

        }


        // Start the process over and wait for the next callback response
        dfs_log(LL_DEBUG3) << "Calling InitCallbackList";
        InitCallbackList();

    }
}

/**
 * This method will start the callback request to the server, requesting
 * an update whenever the server sees that files have been modified.
 *
 * We're making use of a template function here, so that we can keep some
 * of the more intricate workings of the async process out of the way, and
 * give you a chance to focus more on the project's requirements.
 */
void DFSClientNodeP2::InitCallbackList() {
    CallbackList<FileRequestType, FileListResponseType>();
}

//
// STUDENT INSTRUCTION:
//
// Add any additional code you need to here
//

grpc::StatusCode DFSClientNodeP2::HandleBadRPCStatus(const grpc::Status &status, const std::string &filepath) {
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

void DFSClientNodeP2::init_server_map(const FileListResponseType &response, std::unordered_map<std::string, GenericResponse> &server_map) {
    for (const auto& file : response.files()) {
        server_map[file.name()] = file;
    }
}

void DFSClientNodeP2::init_local_map(const std::string &mntPath, std::unordered_map<std::string, struct stat> &local_files) {
    DIR* dir = opendir(mntPath.c_str());
    if (dir == nullptr) {
        dfs_log(LL_ERROR) << "Failed to open directory from init_local_map(): " << mntPath;
        return;
    }
  
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type != DT_REG) {
            continue;
        }

        std::string name = entry->d_name;
        std::string path = WrapPath(name); // get the local path to stat it
        struct stat st;
        if (stat(path.c_str(), &st) == 0) {
            local_files[name] = st; // store the stat struct in the map for the file
        }

    }
    closedir(dir);
}


void DFSClientNodeP2::compare_server_to_local(const std::unordered_map<std::string, GenericResponse> &server_map, const std::unordered_map<std::string, struct stat> &local_files) {
    // 3. Compare the sever files with the local files (going through each file in the server map)
    //    a. If the file is missing locally then fetch it from server
    //    b. If file exists in both:
    //       i. Fetch() if the file from server is newer
    //       ii. Store() if the file from server is older 
    //       iii. Don't care if the files are the same, move on

    for (const auto &serverFile : server_map) {
        const std::string &fileName = serverFile.first;
        const GenericResponse &serverFileInfo = serverFile.second;

        // Get an iterator for the local file map
        auto local_iter = local_files.find(fileName);
        if (local_iter == local_files.end()) {
            // File missing locally, perform Fetch()
            dfs_log(LL_SYSINFO) << "Don't have the file " << fileName << " locally. Fetching from server.";
            Fetch(fileName);
            continue;
        }

        // File exists both locally and on server
        const struct stat &localFileStat = local_iter->second;
        int64_t localMTime = static_cast<int64_t>(localFileStat.st_mtime);
        int64_t serverMTime = serverFileInfo.mtime();

        dfs_log(LL_SYSINFO) << "Comparing file " << fileName << "{ localMTime: " << localMTime << ", serverMTime: " << serverMTime << "}";
        if (localMTime < serverMTime) {
            // The server file is newer, let's perform Fetch()
            dfs_log(LL_SYSINFO) << "The server has a newer version of the file " << fileName << ". Fetching from server.";
            Fetch(fileName);
            continue;
        } else if (localMTime > serverMTime) {
            // The local file is newer, let's perform Store()
            dfs_log(LL_SYSINFO) << "The local node has a newer version of the file " << fileName << ". Sending to the server.";
            Store(fileName);
            continue;
        }
        
        dfs_log(LL_SYSINFO) << "The file " << fileName << " is up to date on both nodes. No need to fetch or store.";
    }
}

void DFSClientNodeP2::compare_local_to_server(const std::unordered_map<std::string, struct stat> &local_files, const std::unordered_map<std::string, GenericResponse> &server_map) {
    // 4. Compare local files to server (going through each item in the local map)
    //    No need to perform check if the file exists on both since we already did that
    //    a. If the file is missing then Store()

    for (const auto &localFile : local_files) {
        const std::string &fileName = localFile.first;
        if (server_map.count(fileName) == 0) {
            dfs_log(LL_SYSINFO) << "The server doesn't have the file : " << fileName << ". Sending to server."; 
            Store(fileName);
        }
    }
}

void DFSClientNodeP2::process_tombstones(const std::vector<std::string> &tombstones, const std::string &mntPath) {
    // Go through the tombstones list and delete them locally
    for (const auto &tombstone : tombstones) {
        std::string filePath = WrapPath(tombstone);
        if (!file_exists(filePath)){
            continue;
        }

        if (std::remove(filePath.c_str()) != 0) {
            dfs_log(LL_ERROR) << "Failed to remove file " << filePath << ". Error: " << strerror(errno);
            continue;
        }

        dfs_log(LL_SYSINFO) << "Successfully removed tombstone file " << filePath;
    }
}

bool DFSClientNodeP2::file_exists(const std::string &filePath) {
    struct stat fstat;
    if (stat(filePath.c_str(), &fstat) == 0) {
        dfs_log(LL_DEBUG2) << "File exists: " << filePath;
        return true;
    }
    dfs_log(LL_DEBUG2) << "File does not exist: " << filePath;
    return false;
}