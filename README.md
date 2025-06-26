# Multithreaded Proxy Server in C

## 🧠 Overview

This project implements a multi-threaded proxy server capable of handling multiple client requests in parallel, with an LRU (Least Recently Used) caching mechanism to store frequently accessed web pages. The server manages concurrency using semaphores and mutexes for thread-safe operations.

---

## 🎯 Learning Goals

- Understand how local computers communicate with remote servers.
- Learn to handle multiple client requests efficiently using threads.
- Manage concurrency with semaphores and mutexes.
- Implement basic caching using an LRU strategy.
- Explore system-level concepts such as multithreading, socket programming, and resource sharing.

---

## ⚙️ Proxy Server Features

- **Speed Enhancement**: Caches frequent requests to reduce server load and latency.
- **Security Extension Ready**: Can be modified to restrict certain domains or add encryption.
- **IP Masking**: Clients connect through the proxy, masking their original IP addresses.

---

## 🧩 OS Concepts Used

- **Multithreading (`pthread`)**
- **Mutex locks** for shared resource protection
- **Semaphores** to limit max client connections
- **LRU Cache** to manage limited storage

---

## 🚫 Limitations

- **Cache Duplication**: Same URL opened by multiple clients may store partial or duplicate cache entries.
- **Fixed Cache Size**: Some large web pages may not be fully cached.

---

## ⚡ How It Works

### 🔄 Request Handling

- Server listens on a specified port for incoming client connections.
- Each connection spawns a thread to handle communication.
- Requests are parsed and forwarded to the actual remote server.
- Responses are returned to the client and optionally cached.

### 💾 Cache Management

- Implemented with LRU (Least Recently Used) policy.
- Cache entries are updated on access and evicted when full.
- Cache operations are mutex-protected for thread safety.

### 🔐 Concurrency Control

- Semaphore restricts concurrent client threads.
- Mutex ensures synchronized access to the cache.

### 🚨 Error Handling

- Handles invalid HTTP versions, unsupported methods (non-GET), and unreachable servers.
- Returns HTTP error codes like 500 for server errors.

---

## 🧱 Components

### 1. Multithreading

- Threads are created using `pthread_create`.
- `sem_wait()` and `sem_post()` control thread entry and exit (instead of `pthread_join`).
- Threads are detached to auto-clean resources.

### 2. Cache (LRU)

- `cache_store` struct holds response data.
- `find(url)` checks cache and updates LRU time.
- `remove_cache_store()` evicts the least recently used entry.
- `add_cache_store()` adds new cache entries (if size permits).

### 3. Synchronization

- Semaphore limits max clients (as defined by `maxClients`).
- Mutex protects cache read/write operations.

---

## 🔧 Project Structure and Functions

### Main Proxy Flow (`proxy_server.c`)

- Creates a server socket.
- Accepts client connections.
- Spawns threads for each request.

### Cache Functions

- `find(char* url)`
- `add_cache_store(char* data, int size, char* url)`
- `remove_cache_store()`

### Remote Server Interaction

- `connectRemoteServer(char* host, int port)`
- `handle_request(clientSocket, request, tempReq)`

### Error Handling

- `sendErrorMessage(clientSocket, status_code)`

---

## 🚀 How to Run

1. **Clone the repository**:
    ```bash
    git clone https://github.com/Sed-Miyuki/Multi_Threaded_Proxy_-LRU-Cache-_C
    cd Multithreaded-Proxy-Server
    ```

2. **Build the project**:
    ```bash
    make all
    ```

3. **Run the proxy server**:
    ```bash
    ./cache_proxy <port>
    ```

    Example:
    ```bash
    ./cache_proxy 8060
    ```

4. **Use in browser**:

    Example usage:
    ```bash
    http://localhost:8060/http://www.cam.ac.uk/
    http://localhost:8060/http://www.testingmcafeesites.com/
    http://localhost:8060/http://www.archive.org
    http://localhost:8060/http://www.cs.washington.edu
    ```

> 📌 **Note:** This code must be run on a **Linux** environment.

---

## 🔮 Future Improvements

- Add support for HTTPS (TLS).
- Allow `POST`, `PUT`, and other HTTP methods.
- Domain filtering/blocklist.
- Replace threading with a **multiprocessing** model for process-level isolation.

---

## 🐞 Known Issues

- **Partial cache**: Large sites may not fully cache due to fixed buffer size.
- **Race conditions**: While mutex-protected, multiple simultaneous cache writes can still degrade performance.

---

## 🧾 Conclusion

This project showcases system-level programming using threads, semaphores, mutexes, sockets, and caching. It's a solid foundation to explore deeper concepts like load balancing, HTTPS, request filtering, and content compression.

It not only builds theoretical understanding of networking but also emphasizes practical performance management and synchronization in multi-client environments.

