# GRPC and Distributed Systems

## Foreward

In this project, you will design and implement a simple distributed file system (DFS).  First, you will develop several file transfer protocols using gRPC and Protocol Buffers. Next, you will incorporate a weakly consistent synchronization system to manage cache consistency between multiple clients and a single server. The system should be able to handle both binary and text-based files.

Your source code will use a combination of C++14, gRPC, and Protocol Buffers to complete the implementation.

## Setup

You can clone the code in the Project 4 repository with the command:

```
git clone https://github.gatech.edu/gios-spr-25/pr4.git
```

## Submission Instructions

Submit all code via the Gradescope. For instructions on how to submit individual components of the assignment, see the instructions within [Part 1](docs/part1.md) and [Part 2](docs/part2.md).

## Readme

Throughout the project, we encourage you to keep notes on what you have done, how you have approached each part of the project, and what resources you used in preparing your work. We have provided you with a prototype file, `readme-student.md` that you should use throughout the project.

Your project readme counts for 10% of your grade for Project 4 and should be submitted on **Canvas** in PDF format. **The project readme file is limited to 12 pages.** Please keep your project readme as short as possible while still covering the requirements of the assignment. There is no limit to the number of times you may submit your readme PDF.

## Directions

- Directions for Part 1 can be found in [docs/part1.md](docs/part1.md)
- Directions for Part 2 can be found in [docs/part2.md](docs/part2.md)

## Log Utility

We've provided a simple logging utility, `dfs_log`, in this assignment that can be used within your project files.

There are five log levels (LL_SYSINFO, LL_ERROR, LL_DEBUG, LL_DEBUG2, and LL_DEBUG3).

During the tests, only log levels LL_SYSINFO, LL_ERROR, and LL_DEBUG will be output. The others will be ignored. You may use the other log levels for your own debugging and testing.

The log utility uses a simple streaming syntax. To use it, make the function call with the log level desired, then stream your messages to it. For example:

```
dfs_log(LL_DEBUG) << "Type your message here: " << add_a_variable << ", add more info, etc."
```

## References

### Relevant lecture material

- [P4L1 Remote Procedure Calls](https://www.udacity.com/course/viewer#!/c-ud923/l-3450238825)

### gRPC and Protocol Buffer resources

- [gRPC C++ Reference](https://grpc.github.io/grpc/cpp/index.html)
- [Protocol Buffers 3 Language Guide](https://developers.google.com/protocol-buffers/docs/proto3)
- [gRPC C++ Examples](https://github.com/grpc/grpc/tree/master/examples/cpp)
- [gRPC C++ Tutorial](https://grpc.io/docs/tutorials/basic/cpp/)
- [Protobuffers Scalar types](https://developers.google.com/protocol-buffers/docs/proto3#scalar)
- [gRPC Status Codes](https://github.com/grpc/grpc/blob/master/doc/statuscodes.md)
- [gRPC Deadline](https://grpc.io/blog/deadlines/)

## Rubric

Your project will be graded at least on the following items:

- Interface specification (.proto)
- Service implementation
- gRPC initiation and binding
- Proper handling of deadline timeouts
- Proper clean up of memory and gRPC resources
- Proper communication with the server
- Proper request and management of write locks
- Proper synchronization of files between multiple clients and a single server
- Proper handling of file deletion requests
- Insightful observations in the Readme file and suggestions for improving the class for future semesters

### gRPC Implementation (35 points)

Full credit requires: code compiles successfully, does not crash, files fully transmitted, basic safety checks, and proper use of gRPC  - including the ability to get, store, delete, and list files, along with the ability to recognize a timeout. Note that the automated tests will test some of these automatically, but graders may execute additional tests of these requirements.

### DFS Implementation (55 points + 10 points extra credit)

Full credit requires: code compiles successfully, does not crash, files fully transmitted, basic safety checks, proper use of gRPC, write locks properly handled, cache properly handled, synchronization of sync and inotify threads properly handled, and synchronization of multiple clients to a single server. Note that the automated tests will test some of these automatically, but graders may execute additional tests of these requirements.

Extra Credit: In addition to the standard tests, there are 10 points of extra credit involving ensuring that multiple clients can properly sync and that inotify events are properly coordinated.

### README (10 points + 5 point extra credit opportunity)

You must submit:

- A file named `yourgtaccount-analysis.pdf` containing your writeup (GT account is what you log in with, not your all-digits ID. Example: `jdoe1-analysis.pdf`). This file will be submitted **via Canvas** as a PDF (Project 4 README assignment). You may use any tool you desire to create it, so long as it is a compliant PDF - and for us. Compliant means "we can open it using Acrobat Reader".

**The project readme is limited to 12 pages.** Please keep your readme as short as possible
while still covering all requirements of the assignment. Points will be deducted if the project
readme exceeds the page limit.

**NOTE:** Some of the best readme files submitted are not long, but short, clear, and concise,
including all requirements. This approach is particularly effective for a Georgia Tech
graduate-level class, where clarity and conciseness are highly valued.

Your readme should include and demonstrate the following:

- Clearly demonstrates your understanding of what you did and why - we want to see your design and your explanation of the choices that you made and why you made those choices. (4 points)
  
- A description of the flow of control for your code; we strongly suggest that you use graphics here, but a thorough textual explanation is sufficient. (2 points)
  
- A brief explanation of how you implemented and tested your code. (2 points)
  
- References any external materials that you consulted during your development process (2 points)

- Suggestions on how you would improve the documentation, sample code, testing, or other aspects of the project (up to 5 points extra credit available for noteworthy suggestions here, e.g., actual descriptions of how you would change things, sample code, code for tests, etc.) We do not give extra credit for simply reporting an issue - we're looking for actionable suggestions on how to improve things.

## Questions

For all questions, please use the class Piazza forum or the class Slack channel so that TA's and other students can assist you.
