# Object-Oriented Social Network

A terminal-based C++ social networking application built around object-oriented design, relationship management, and file-backed persistence.

## Features

- Account registration, validation, login, and profile updates
- User search and profile discovery
- Post creation and personal timelines
- Likes and comments on user posts
- Friend requests, approvals, and friend lists
- Direct messages between connected users
- Group creation, discovery, membership, and discussions
- Page creation, discovery, and membership
- Persistent users, posts, relationships, chats, groups, and pages
- Interactive terminal session with a clear command menu

## Core Model

| Component | Responsibility |
| --- | --- |
| `User` | Account identity, credentials, and profile information |
| `Post` | User-authored timeline content |
| `Comment` | Responses attached to posts |
| `Message` | Timestamped direct and group messages |
| `Community` | Shared model for groups and public-interest pages |
| `SocialNetwork` | Validation, authentication, persistence, relationships, content, and session workflows |

## Technology Stack

| Layer | Technology |
| --- | --- |
| Language | C++17 |
| Interface | Interactive terminal |
| Architecture | Object-oriented classes and service-style manager |
| Persistence | Atomically written, encoded TSV records |
| Build | CMake or direct compiler invocation |
| Supported Compilers | GCC, MinGW, MSVC, Clang |

## Project Structure

```text
.
|-- src/
|   `-- main.cpp
|-- .vscode/
|   |-- tasks.json
|   `-- launch.json
|-- CMakeLists.txt
|-- .gitignore
`-- README.md
```

Runtime records are created automatically inside the ignored `data/` directory.

## Build With CMake

```bash
cmake -S . -B build
cmake --build build
```

Run on Windows with a Visual Studio generator:

```powershell
.\build\Debug\social-network.exe
```

Run with a single-configuration generator:

```powershell
.\build\social-network.exe
```

Run on Linux or macOS:

```bash
./build/social-network
```

## Build Directly With MinGW

```powershell
g++ -std=c++17 -Wall -Wextra -pedantic src/*.cpp -o SocialNetwork.exe
.\SocialNetwork.exe
```

## Build Directly On Linux

```bash
g++ -std=c++17 -Wall -Wextra -pedantic src/*.cpp -o social-network
./social-network
```

## Run In VS Code

The included VS Code configuration targets the Microsoft C++ debugger:

- `Ctrl+Shift+B` builds the application
- `F5` builds and launches it in the integrated terminal

Update the compiler path in `.vscode/tasks.json` when Visual Studio is installed in a different location.

## Application Workflow

1. Start the program and choose `signup` or `login`.
2. Register with a valid password and `YYYY-MM-DD` birthdate.
3. Sign in to open the persistent session menu.
4. Create posts, search users, manage friendships, message friends, or browse groups and pages.
5. Use `logout` to return to the authentication screen.
6. Use `exit` to close the application.

## Persistence

Runtime state is stored in the ignored `data/` directory:

- Records use percent-encoded, tab-separated fields so user input cannot corrupt the file structure.
- Writes use temporary files and atomic replacement to reduce partial-write corruption.
- User IDs include time and random entropy, avoiding same-second collisions.
- Relationships, reactions, memberships, and requests are deduplicated.
- Passwords are stored as salted digests rather than plaintext.

The credential digest is intentionally dependency-free for this educational terminal project. A network-facing production system should replace it with Argon2id, scrypt, or bcrypt and use a transactional database.

## Verification

Run the built-in end-to-end self-test:

```powershell
.\SocialNetwork.exe --self-test
```

The test validates registration, duplicate prevention, login, posts, unique reactions, comments, friendships, messaging, communities, and persistence after reloading the application.
