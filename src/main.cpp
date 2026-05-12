#include <algorithm>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

using namespace std;

namespace {

const string DataDirectory = "data";

void ensureDirectory(const string& path) {
#ifdef _WIN32
    const int result = _mkdir(path.c_str());
#else
    const int result = mkdir(path.c_str(), 0755);
#endif
    if (result != 0 && errno != EEXIST) {
        throw runtime_error("Unable to create directory: " + path);
    }
}

string encode(const string& value) {
    ostringstream output;
    output << hex << uppercase << setfill('0');
    for (unsigned char character : value) {
        if (isalnum(character) || character == '-' || character == '_' || character == '.' || character == '@') {
            output << character;
        } else {
            output << '%' << setw(2) << static_cast<int>(character);
        }
    }
    return output.str();
}

string decode(const string& value) {
    string output;
    for (size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '%' && index + 2 < value.size()) {
            unsigned int code = 0;
            istringstream input(value.substr(index + 1, 2));
            input >> hex >> code;
            if (!input.fail()) {
                output.push_back(static_cast<char>(code));
                index += 2;
                continue;
            }
        }
        output.push_back(value[index]);
    }
    return output;
}

vector<string> split(const string& line, char delimiter = '\t') {
    vector<string> parts;
    string part;
    istringstream input(line);
    while (getline(input, part, delimiter)) {
        parts.push_back(decode(part));
    }
    return parts;
}

string join(const vector<string>& fields, char delimiter = '\t') {
    ostringstream output;
    for (size_t index = 0; index < fields.size(); ++index) {
        if (index) output << delimiter;
        output << encode(fields[index]);
    }
    return output.str();
}

void atomicWrite(const string& path, const vector<string>& lines) {
    const string temporary = path + ".tmp";
    {
        ofstream output(temporary.c_str(), ios::trunc);
        if (!output) throw runtime_error("Unable to write: " + temporary);
        for (const string& line : lines) output << line << '\n';
        if (!output.good()) throw runtime_error("Failed while writing: " + temporary);
    }
    remove(path.c_str());
    if (rename(temporary.c_str(), path.c_str()) != 0) {
        remove(temporary.c_str());
        throw runtime_error("Unable to replace: " + path);
    }
}

vector<string> readLines(const string& path) {
    vector<string> lines;
    ifstream input(path.c_str());
    string line;
    while (getline(input, line)) {
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

string lower(string value) {
    transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(tolower(character));
    });
    return value;
}

string uniqueId(const string& prefix) {
    static unsigned long counter = 0;
    static mt19937_64 generator(
        static_cast<unsigned long long>(chrono::high_resolution_clock::now().time_since_epoch().count()));
    const unsigned long long now =
        static_cast<unsigned long long>(chrono::high_resolution_clock::now().time_since_epoch().count());
    ostringstream output;
    output << prefix << hex << now << generator() << ++counter;
    return output.str();
}

string randomSalt() {
    return uniqueId("salt");
}

// Repeated salted hashing prevents credentials from being stored as plaintext.
// A production service should use a dedicated password KDF such as Argon2id.
string passwordDigest(const string& password, const string& salt) {
    string value = salt + ":" + password;
    hash<string> hasher;
    for (int round = 0; round < 120000; ++round) {
        const size_t digest = hasher(value + salt + to_string(round));
        ostringstream output;
        output << hex << digest;
        value = output.str();
    }
    return value;
}

bool validEmail(const string& email) {
    const size_t at = email.find('@');
    const size_t dot = email.rfind('.');
    return at > 0 && dot != string::npos && dot > at + 1 && dot + 1 < email.size();
}

bool validPassword(const string& password) {
    if (password.size() < 8) return false;
    bool upper = false, lowerCase = false, digit = false;
    for (unsigned char character : password) {
        upper = upper || isupper(character);
        lowerCase = lowerCase || islower(character);
        digit = digit || isdigit(character);
    }
    return upper && lowerCase && digit;
}

bool leapYear(int year) {
    return year % 400 == 0 || (year % 4 == 0 && year % 100 != 0);
}

bool validBirthdate(const string& value) {
    if (value.size() != 10 || value[4] != '-' || value[7] != '-') return false;
    try {
        const int year = stoi(value.substr(0, 4));
        const int month = stoi(value.substr(5, 2));
        const int day = stoi(value.substr(8, 2));
        if (year < 1900 || month < 1 || month > 12 || day < 1) return false;
        const int days[] = {31, leapYear(year) ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        return day <= days[month - 1];
    } catch (...) {
        return false;
    }
}

string promptLine(const string& label) {
    cout << label;
    string value;
    getline(cin, value);
    return value;
}

int promptNumber(const string& label, int minimum, int maximum) {
    while (true) {
        const string value = promptLine(label);
        try {
            size_t consumed = 0;
            const int result = stoi(value, &consumed);
            if (consumed == value.size() && result >= minimum && result <= maximum) return result;
        } catch (...) {
        }
        cout << "Enter a number from " << minimum << " to " << maximum << ".\n";
    }
}

struct User {
    string id;
    string name;
    string email;
    string salt;
    string passwordHash;
    string birthdate;
};

struct Post {
    string id;
    string authorId;
    string content;
};

struct Community {
    string id;
    string ownerId;
    string name;
    string description;
};

class SocialNetwork {
public:
    explicit SocialNetwork(string directory = DataDirectory) : directory_(directory) {
        ensureDirectory(directory_);
        load();
    }

    bool registerUser(const string& name, const string& email, const string& password,
                      const string& birthdate, string& message) {
        if (name.empty()) return fail(message, "Name is required.");
        if (!validEmail(email)) return fail(message, "Enter a valid email address.");
        if (!validPassword(password)) return fail(message, "Password requires 8 characters, uppercase, lowercase, and a digit.");
        if (!validBirthdate(birthdate)) return fail(message, "Birthdate must be a real date in YYYY-MM-DD format.");
        if (findUserByEmail(email)) return fail(message, "An account already exists for this email.");

        const string salt = randomSalt();
        users_.push_back({uniqueId("user"), name, lower(email), salt, passwordDigest(password, salt), birthdate});
        saveUsers();
        message = "Account created successfully.";
        return true;
    }

    const User* login(const string& email, const string& password) const {
        const User* user = findUserByEmail(email);
        if (!user || passwordDigest(password, user->salt) != user->passwordHash) return nullptr;
        return user;
    }

    bool updateProfile(const string& userId, const string& name, const string& password,
                       const string& birthdate, string& message) {
        User* user = findUser(userId);
        if (!user) return fail(message, "User not found.");
        if (!name.empty()) user->name = name;
        if (!password.empty()) {
            if (!validPassword(password)) return fail(message, "The new password is too weak.");
            user->salt = randomSalt();
            user->passwordHash = passwordDigest(password, user->salt);
        }
        if (!birthdate.empty()) {
            if (!validBirthdate(birthdate)) return fail(message, "The birthdate is invalid.");
            user->birthdate = birthdate;
        }
        saveUsers();
        message = "Profile updated.";
        return true;
    }

    vector<User> searchUsers(const string& query, const string& excludeId = "") const {
        vector<User> matches;
        const string needle = lower(query);
        for (const User& user : users_) {
            if (user.id != excludeId &&
                (lower(user.name).find(needle) != string::npos || lower(user.email).find(needle) != string::npos)) {
                matches.push_back(user);
            }
        }
        return matches;
    }

    bool createPost(const string& authorId, const string& content, string& message) {
        if (!findUser(authorId)) return fail(message, "User not found.");
        if (content.empty()) return fail(message, "Post content cannot be empty.");
        posts_.push_back({uniqueId("post"), authorId, content});
        savePosts();
        message = "Post published.";
        return true;
    }

    vector<Post> postsFor(const string& userId) const {
        vector<Post> result;
        for (const Post& post : posts_) if (post.authorId == userId) result.push_back(post);
        return result;
    }

    bool likePost(const string& userId, const string& postId, string& message) {
        if (!findUser(userId) || !findPost(postId)) return fail(message, "User or post not found.");
        const string key = userId + "\t" + postId;
        if (!likes_.insert(key).second) return fail(message, "You already liked this post.");
        saveSet("likes.tsv", likes_);
        message = "Post liked.";
        return true;
    }

    bool comment(const string& userId, const string& postId, const string& content, string& message) {
        if (!findUser(userId) || !findPost(postId)) return fail(message, "User or post not found.");
        if (content.empty()) return fail(message, "Comment cannot be empty.");
        comments_.push_back(join({uniqueId("comment"), userId, postId, content}));
        atomicWrite(path("comments.tsv"), comments_);
        message = "Comment added.";
        return true;
    }

    int likeCount(const string& postId) const {
        int count = 0;
        for (const string& entry : likes_) if (split(entry).size() == 2 && split(entry)[1] == postId) ++count;
        return count;
    }

    vector<vector<string>> commentsFor(const string& postId) const {
        vector<vector<string>> result;
        for (const string& line : comments_) {
            vector<string> fields = split(line);
            if (fields.size() == 4 && fields[2] == postId) result.push_back(fields);
        }
        return result;
    }

    bool sendFriendRequest(const string& from, const string& to, string& message) {
        if (from == to) return fail(message, "You cannot add yourself.");
        if (!findUser(from) || !findUser(to)) return fail(message, "User not found.");
        if (friends(from, to)) return fail(message, "You are already friends.");
        const string request = from + "\t" + to;
        if (!requests_.insert(request).second) return fail(message, "Friend request already sent.");
        saveSet("friend_requests.tsv", requests_);
        message = "Friend request sent.";
        return true;
    }

    vector<User> incomingRequests(const string& userId) const {
        vector<User> result;
        for (const string& request : requests_) {
            vector<string> fields = split(request);
            if (fields.size() == 2 && fields[1] == userId) {
                const User* user = findUser(fields[0]);
                if (user) result.push_back(*user);
            }
        }
        return result;
    }

    bool respondToRequest(const string& userId, const string& requesterId, bool accept, string& message) {
        const string request = requesterId + "\t" + userId;
        if (!requests_.erase(request)) return fail(message, "Friend request not found.");
        if (accept) friendships_.insert(friendKey(userId, requesterId));
        saveSet("friend_requests.tsv", requests_);
        saveSet("friendships.tsv", friendships_);
        message = accept ? "Friend request accepted." : "Friend request declined.";
        return true;
    }

    bool friends(const string& first, const string& second) const {
        return friendships_.count(friendKey(first, second)) > 0;
    }

    vector<User> friendList(const string& userId) const {
        vector<User> result;
        for (const string& relation : friendships_) {
            vector<string> fields = split(relation);
            if (fields.size() != 2) continue;
            string other;
            if (fields[0] == userId) other = fields[1];
            if (fields[1] == userId) other = fields[0];
            if (!other.empty()) {
                const User* user = findUser(other);
                if (user) result.push_back(*user);
            }
        }
        return result;
    }

    bool sendMessage(const string& from, const string& to, const string& content, string& message) {
        if (!friends(from, to)) return fail(message, "Messages are limited to friends.");
        if (content.empty()) return fail(message, "Message cannot be empty.");
        messages_.push_back(join({uniqueId("message"), from, to, content}));
        atomicWrite(path("messages.tsv"), messages_);
        message = "Message sent.";
        return true;
    }

    vector<vector<string>> conversation(const string& first, const string& second) const {
        vector<vector<string>> result;
        for (const string& line : messages_) {
            vector<string> fields = split(line);
            if (fields.size() == 4 &&
                ((fields[1] == first && fields[2] == second) || (fields[1] == second && fields[2] == first))) {
                result.push_back(fields);
            }
        }
        return result;
    }

    bool createCommunity(bool group, const string& ownerId, const string& name,
                         const string& description, string& message) {
        if (!findUser(ownerId)) return fail(message, "User not found.");
        if (name.empty()) return fail(message, "Name is required.");
        vector<Community>& communities = group ? groups_ : pages_;
        communities.push_back({uniqueId(group ? "group" : "page"), ownerId, name, description});
        memberships_.insert(communities.back().id + "\t" + ownerId);
        saveCommunities(group);
        saveSet("memberships.tsv", memberships_);
        message = group ? "Group created." : "Page created.";
        return true;
    }

    vector<Community> searchCommunities(bool group, const string& query) const {
        vector<Community> result;
        const vector<Community>& source = group ? groups_ : pages_;
        const string needle = lower(query);
        for (const Community& community : source) {
            if (lower(community.name).find(needle) != string::npos ||
                lower(community.description).find(needle) != string::npos) {
                result.push_back(community);
            }
        }
        return result;
    }

    bool setMembership(const string& userId, const string& communityId, bool join, string& message) {
        if (!findUser(userId) || !findCommunity(communityId)) return fail(message, "User or community not found.");
        const string key = communityId + "\t" + userId;
        if (join && !memberships_.insert(key).second) return fail(message, "You are already a member.");
        if (!join && !memberships_.erase(key)) return fail(message, "You are not a member.");
        saveSet("memberships.tsv", memberships_);
        message = join ? "Membership added." : "Membership removed.";
        return true;
    }

    bool member(const string& userId, const string& communityId) const {
        return memberships_.count(communityId + "\t" + userId) > 0;
    }

    bool postGroupMessage(const string& userId, const string& groupId, const string& content, string& message) {
        if (!member(userId, groupId)) return fail(message, "Join the group before posting.");
        if (content.empty()) return fail(message, "Message cannot be empty.");
        groupMessages_.push_back(join({uniqueId("discussion"), groupId, userId, content}));
        atomicWrite(path("group_messages.tsv"), groupMessages_);
        message = "Group message posted.";
        return true;
    }

    vector<vector<string>> groupMessages(const string& groupId) const {
        vector<vector<string>> result;
        for (const string& line : groupMessages_) {
            vector<string> fields = split(line);
            if (fields.size() == 4 && fields[1] == groupId) result.push_back(fields);
        }
        return result;
    }

    string userName(const string& id) const {
        const User* user = findUser(id);
        return user ? user->name : "Unknown user";
    }

private:
    string directory_;
    vector<User> users_;
    vector<Post> posts_;
    vector<Community> groups_;
    vector<Community> pages_;
    set<string> likes_;
    set<string> requests_;
    set<string> friendships_;
    set<string> memberships_;
    vector<string> comments_;
    vector<string> messages_;
    vector<string> groupMessages_;

    string path(const string& file) const { return directory_ + "/" + file; }

    static bool fail(string& message, const string& value) {
        message = value;
        return false;
    }

    static string friendKey(string first, string second) {
        if (second < first) swap(first, second);
        return first + "\t" + second;
    }

    User* findUser(const string& id) {
        for (User& user : users_) if (user.id == id) return &user;
        return nullptr;
    }

    const User* findUser(const string& id) const {
        for (const User& user : users_) if (user.id == id) return &user;
        return nullptr;
    }

    const User* findUserByEmail(const string& email) const {
        const string normalized = lower(email);
        for (const User& user : users_) if (lower(user.email) == normalized) return &user;
        return nullptr;
    }

    const Post* findPost(const string& id) const {
        for (const Post& post : posts_) if (post.id == id) return &post;
        return nullptr;
    }

    const Community* findCommunity(const string& id) const {
        for (const Community& item : groups_) if (item.id == id) return &item;
        for (const Community& item : pages_) if (item.id == id) return &item;
        return nullptr;
    }

    void load() {
        for (const string& line : readLines(path("users.tsv"))) {
            vector<string> fields = split(line);
            if (fields.size() == 6) users_.push_back({fields[0], fields[1], fields[2], fields[3], fields[4], fields[5]});
        }
        for (const string& line : readLines(path("posts.tsv"))) {
            vector<string> fields = split(line);
            if (fields.size() == 3 && findUser(fields[1])) posts_.push_back({fields[0], fields[1], fields[2]});
        }
        loadCommunities(true);
        loadCommunities(false);
        loadSet("likes.tsv", likes_);
        loadSet("friend_requests.tsv", requests_);
        loadSet("friendships.tsv", friendships_);
        loadSet("memberships.tsv", memberships_);
        comments_ = readLines(path("comments.tsv"));
        messages_ = readLines(path("messages.tsv"));
        groupMessages_ = readLines(path("group_messages.tsv"));
    }

    void loadCommunities(bool group) {
        vector<Community>& target = group ? groups_ : pages_;
        for (const string& line : readLines(path(group ? "groups.tsv" : "pages.tsv"))) {
            vector<string> fields = split(line);
            if (fields.size() == 4 && findUser(fields[1])) target.push_back({fields[0], fields[1], fields[2], fields[3]});
        }
    }

    void loadSet(const string& file, set<string>& target) {
        for (const string& line : readLines(path(file))) {
            vector<string> fields = split(line);
            if (fields.size() == 2) target.insert(fields[0] + "\t" + fields[1]);
        }
    }

    void saveUsers() const {
        vector<string> lines;
        for (const User& user : users_) {
            lines.push_back(join({user.id, user.name, user.email, user.salt, user.passwordHash, user.birthdate}));
        }
        atomicWrite(path("users.tsv"), lines);
    }

    void savePosts() const {
        vector<string> lines;
        for (const Post& post : posts_) lines.push_back(join({post.id, post.authorId, post.content}));
        atomicWrite(path("posts.tsv"), lines);
    }

    void saveCommunities(bool group) const {
        vector<string> lines;
        const vector<Community>& source = group ? groups_ : pages_;
        for (const Community& item : source) lines.push_back(join({item.id, item.ownerId, item.name, item.description}));
        atomicWrite(path(group ? "groups.tsv" : "pages.tsv"), lines);
    }

    void saveSet(const string& file, const set<string>& values) const {
        vector<string> lines;
        for (const string& value : values) {
            vector<string> fields = split(value);
            if (fields.size() == 2) lines.push_back(join(fields));
        }
        atomicWrite(path(file), lines);
    }
};

void showPosts(const SocialNetwork& network, const vector<Post>& posts) {
    if (posts.empty()) {
        cout << "No posts found.\n";
        return;
    }
    for (size_t index = 0; index < posts.size(); ++index) {
        const Post& post = posts[index];
        cout << index + 1 << ". " << post.content << " | " << network.likeCount(post.id) << " like(s)\n";
        for (const vector<string>& comment : network.commentsFor(post.id)) {
            cout << "   - " << network.userName(comment[1]) << ": " << comment[3] << '\n';
        }
    }
}

void userSession(SocialNetwork& network, string userId) {
    while (true) {
        cout << "\n============================================================\n";
        cout << "  " << network.userName(userId) << "'s workspace\n";
        cout << "============================================================\n";
        cout << "  post | my_posts | search | requests | friends\n";
        cout << "  groups | pages | update | logout\n> ";
        string command;
        getline(cin, command);
        command = lower(command);
        string message;

        if (command == "logout") return;
        if (command == "post") {
            network.createPost(userId, promptLine("What's on your mind?\n> "), message);
            cout << message << '\n';
        } else if (command == "my_posts") {
            showPosts(network, network.postsFor(userId));
        } else if (command == "update") {
            network.updateProfile(userId, promptLine("New name (blank to keep): "),
                                  promptLine("New password (blank to keep): "),
                                  promptLine("New birthdate (blank to keep): "), message);
            cout << message << '\n';
        } else if (command == "search") {
            vector<User> users = network.searchUsers(promptLine("Name or email: "), userId);
            if (users.empty()) {
                cout << "No matching users.\n";
                continue;
            }
            for (size_t index = 0; index < users.size(); ++index) {
                cout << index + 1 << ". " << users[index].name << " <" << users[index].email << ">\n";
            }
            const User& selected = users[promptNumber("Select user: ", 1, static_cast<int>(users.size())) - 1];
            vector<Post> posts = network.postsFor(selected.id);
            showPosts(network, posts);
            if (!network.friends(userId, selected.id) && lower(promptLine("Send friend request? (yes/no): ")) == "yes") {
                network.sendFriendRequest(userId, selected.id, message);
                cout << message << '\n';
            }
            if (!posts.empty() && lower(promptLine("Interact with a post? (yes/no): ")) == "yes") {
                const Post& post = posts[promptNumber("Post number: ", 1, static_cast<int>(posts.size())) - 1];
                if (lower(promptLine("Like it? (yes/no): ")) == "yes") {
                    network.likePost(userId, post.id, message);
                    cout << message << '\n';
                }
                const string comment = promptLine("Comment (blank to skip): ");
                if (!comment.empty()) {
                    network.comment(userId, post.id, comment, message);
                    cout << message << '\n';
                }
            }
        } else if (command == "requests") {
            vector<User> requests = network.incomingRequests(userId);
            if (requests.empty()) {
                cout << "No pending friend requests.\n";
                continue;
            }
            for (size_t index = 0; index < requests.size(); ++index) cout << index + 1 << ". " << requests[index].name << '\n';
            const User& selected = requests[promptNumber("Select request: ", 1, static_cast<int>(requests.size())) - 1];
            const bool accept = lower(promptLine("Accept? (yes/no): ")) == "yes";
            network.respondToRequest(userId, selected.id, accept, message);
            cout << message << '\n';
        } else if (command == "friends") {
            vector<User> friends = network.friendList(userId);
            if (friends.empty()) {
                cout << "No friends yet.\n";
                continue;
            }
            for (size_t index = 0; index < friends.size(); ++index) cout << index + 1 << ". " << friends[index].name << '\n';
            const User& selected = friends[promptNumber("Open conversation: ", 1, static_cast<int>(friends.size())) - 1];
            for (const vector<string>& entry : network.conversation(userId, selected.id)) {
                cout << network.userName(entry[1]) << ": " << entry[3] << '\n';
            }
            const string content = promptLine("Message (blank to close): ");
            if (!content.empty()) {
                network.sendMessage(userId, selected.id, content, message);
                cout << message << '\n';
            }
        } else if (command == "groups" || command == "pages") {
            const bool group = command == "groups";
            const string action = lower(promptLine(group ? "[create] [search]: " : "[create] [search]: "));
            if (action == "create") {
                network.createCommunity(group, userId, promptLine("Name: "), promptLine("Description: "), message);
                cout << message << '\n';
                continue;
            }
            vector<Community> communities = network.searchCommunities(group, promptLine("Search: "));
            if (communities.empty()) {
                cout << "No matching communities.\n";
                continue;
            }
            for (size_t index = 0; index < communities.size(); ++index) {
                cout << index + 1 << ". " << communities[index].name << " - " << communities[index].description << '\n';
            }
            const Community& selected =
                communities[promptNumber("Select: ", 1, static_cast<int>(communities.size())) - 1];
            if (network.member(userId, selected.id)) {
                if (group) {
                    for (const vector<string>& entry : network.groupMessages(selected.id)) {
                        cout << network.userName(entry[2]) << ": " << entry[3] << '\n';
                    }
                    const string content = promptLine("Discussion message (blank to skip): ");
                    if (!content.empty()) {
                        network.postGroupMessage(userId, selected.id, content, message);
                        cout << message << '\n';
                    }
                }
                if (lower(promptLine("Leave this community? (yes/no): ")) == "yes") {
                    network.setMembership(userId, selected.id, false, message);
                    cout << message << '\n';
                }
            } else if (lower(promptLine("Join this community? (yes/no): ")) == "yes") {
                network.setMembership(userId, selected.id, true, message);
                cout << message << '\n';
            }
        } else {
            cout << "Unknown command.\n";
        }
    }
}

int runSelfTest() {
    const string directory = "self-test-data";
    ensureDirectory(directory);
    const vector<string> files = {
        "users.tsv", "posts.tsv", "groups.tsv", "pages.tsv", "likes.tsv", "comments.tsv",
        "friend_requests.tsv", "friendships.tsv", "messages.tsv", "memberships.tsv", "group_messages.tsv"
    };
    for (const string& file : files) remove((directory + "/" + file).c_str());

    SocialNetwork network(directory);
    string result;
    if (!network.registerUser("Alice Smith", "alice@example.com", "StrongPass1", "2000-02-29", result)) return 1;
    if (!network.registerUser("Bob Jones", "bob@example.com", "StrongPass2", "1999-12-31", result)) return 2;
    if (network.registerUser("Duplicate", "ALICE@example.com", "StrongPass3", "2000-01-01", result)) return 3;
    const User* alice = network.login("alice@example.com", "StrongPass1");
    const User* bob = network.login("bob@example.com", "StrongPass2");
    if (!alice || !bob || network.login("alice@example.com", "wrong")) return 4;
    const string aliceId = alice->id;
    const string bobId = bob->id;
    if (!network.createPost(aliceId, "Hello, network!", result)) return 5;
    const vector<Post> posts = network.postsFor(aliceId);
    if (posts.size() != 1 || !network.likePost(bobId, posts[0].id, result)) return 6;
    if (network.likePost(bobId, posts[0].id, result)) return 7;
    if (!network.comment(bobId, posts[0].id, "Welcome!", result)) return 8;
    if (!network.sendFriendRequest(aliceId, bobId, result)) return 9;
    if (network.sendFriendRequest(aliceId, bobId, result)) return 10;
    if (!network.respondToRequest(bobId, aliceId, true, result) || !network.friends(aliceId, bobId)) return 11;
    if (!network.sendMessage(aliceId, bobId, "Hi Bob", result)) return 12;
    if (network.conversation(bobId, aliceId).size() != 1) return 13;
    if (!network.createCommunity(true, aliceId, "C++ Club", "Systems programming", result)) return 14;
    const vector<Community> groups = network.searchCommunities(true, "c++");
    if (groups.size() != 1 || !network.setMembership(bobId, groups[0].id, true, result)) return 15;
    if (!network.postGroupMessage(bobId, groups[0].id, "Hello club", result)) return 16;
    if (!network.createCommunity(false, aliceId, "Engineering", "Campus engineering page", result)) return 17;

    SocialNetwork reloaded(directory);
    if (!reloaded.login("alice@example.com", "StrongPass1")) return 18;
    if (!reloaded.friends(aliceId, bobId)) return 19;
    if (reloaded.postsFor(aliceId).size() != 1) return 20;
    if (reloaded.conversation(aliceId, bobId).size() != 1) return 21;
    if (reloaded.searchCommunities(true, "club").size() != 1) return 22;

    cout << "All social-network self-tests passed.\n";
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc > 1 && string(argv[1]) == "--self-test") return runSelfTest();

        SocialNetwork network;
        cout << "============================================================\n";
        cout << "        Object-Oriented Social Network - Terminal App       \n";
        cout << "============================================================\n";

        while (true) {
            const string action = lower(promptLine("\n[login] [signup] [exit]\n> "));
            if (action == "exit") break;
            string message;
            if (action == "signup") {
                network.registerUser(promptLine("Full name: "), promptLine("Email: "),
                                     promptLine("Password: "), promptLine("Birthdate (YYYY-MM-DD): "), message);
                cout << message << '\n';
            } else if (action == "login") {
                const User* user = network.login(promptLine("Email: "), promptLine("Password: "));
                if (!user) {
                    cout << "Invalid email or password.\n";
                } else {
                    userSession(network, user->id);
                }
            } else {
                cout << "Unknown command.\n";
            }
        }
        cout << "Goodbye!\n";
        return 0;
    } catch (const exception& error) {
        cerr << "Application error: " << error.what() << '\n';
        return 1;
    }
}
