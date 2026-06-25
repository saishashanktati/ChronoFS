/*
 * ============================================================================
 *  In-Memory Git-Inspired Version Control File System
 * ============================================================================
 *  Custom data structures (NO STL containers used for the core engine):
 *    1. HashMap<K,V>   -> custom hash table (separate chaining + rehashing)
 *                         used for: blob storage, commit storage, branch
 *                         table, directory children, path index (fast lookup)
 *    2. FileNode (Tree)-> custom N-ary tree representing the directory
 *                         hierarchy (like Git's tree objects). Each commit
 *                         stores its own lightweight snapshot tree, while
 *                         actual file contents are content-addressed and
 *                         deduplicated in a shared blob store (like Git blobs)
 *    3. Heap<T,Cmp>    -> custom binary max-heap used for:
 *                         - chronological commit-history traversal (log)
 *                         - multi-branch chronological merge (logall)
 *                         - real-time "most recently modified files" report
 *
 *  Features: branching, snapshot/commit creation, rollback, branch
 *  checkout, diffing, and a command-driven REPL for historical inspection.
 *
 *  Build:   g++ -std=c++17 -O2 -o vcs version_control_system.cpp
 *  Run:     ./vcs                (interactive REPL, type `help`)
 *           ./vcs --demo         (runs a scripted demo automatically)
 * ============================================================================
 */

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <utility>
#include <algorithm>
#include <iomanip>

using namespace std;

// ============================================================================
// 1. CUSTOM HASH FUNCTOR
// ============================================================================
struct CustomHash {
    // FNV-1a 64-bit hash for strings
    size_t operator()(const string& key) const {
        unsigned long long hash = 14695981039346656037ULL;
        for (unsigned char c : key) {
            hash ^= c;
            hash *= 1099511628211ULL;
        }
        return (size_t)hash;
    }
    size_t operator()(int key) const {
        return (size_t)key;
    }
};

// ============================================================================
// 2. CUSTOM HASHMAP  (separate chaining, dynamic rehashing)
// ============================================================================
template <typename K, typename V, typename Hash = CustomHash>
class HashMap {
private:
    struct Entry {
        K key;
        V value;
        Entry* next;
        Entry(const K& k, const V& v) : key(k), value(v), next(nullptr) {}
    };

    vector<Entry*> buckets;
    size_t bucketCount;
    size_t entryCount;
    Hash hashFn;
    static constexpr double LOAD_FACTOR = 0.75;

    size_t indexFor(const K& key, size_t mod) const {
        return hashFn(key) % mod;
    }

    void rehash() {
        size_t newCount = bucketCount * 2;
        vector<Entry*> newBuckets(newCount, nullptr);
        for (size_t i = 0; i < bucketCount; i++) {
            Entry* curr = buckets[i];
            while (curr) {
                Entry* nxt = curr->next;
                size_t idx = indexFor(curr->key, newCount);
                curr->next = newBuckets[idx];
                newBuckets[idx] = curr;
                curr = nxt;
            }
        }
        buckets = std::move(newBuckets);
        bucketCount = newCount;
    }

public:
    explicit HashMap(size_t initialCapacity = 16)
        : bucketCount(initialCapacity ? initialCapacity : 16), entryCount(0) {
        buckets.assign(bucketCount, nullptr);
    }

    HashMap(const HashMap&) = delete;
    HashMap& operator=(const HashMap&) = delete;

    HashMap(HashMap&& other) noexcept
        : buckets(std::move(other.buckets)),
          bucketCount(other.bucketCount),
          entryCount(other.entryCount) {
        other.bucketCount = 0;
        other.entryCount = 0;
    }

    HashMap& operator=(HashMap&& other) noexcept {
        if (this != &other) {
            clear();
            buckets = std::move(other.buckets);
            bucketCount = other.bucketCount;
            entryCount = other.entryCount;
            other.bucketCount = 0;
            other.entryCount = 0;
        }
        return *this;
    }

    ~HashMap() { clear(); }

    void put(const K& key, const V& value) {
        size_t idx = indexFor(key, bucketCount);
        Entry* curr = buckets[idx];
        while (curr) {
            if (curr->key == key) { curr->value = value; return; }
            curr = curr->next;
        }
        Entry* e = new Entry(key, value);
        e->next = buckets[idx];
        buckets[idx] = e;
        entryCount++;
        if ((double)entryCount / (double)bucketCount > LOAD_FACTOR) rehash();
    }

    bool get(const K& key, V& out) const {
        size_t idx = indexFor(key, bucketCount);
        Entry* curr = buckets[idx];
        while (curr) {
            if (curr->key == key) { out = curr->value; return true; }
            curr = curr->next;
        }
        return false;
    }

    // Returns a pointer to the stored value for in-place lookup/mutation.
    V* getPtr(const K& key) const {
        size_t idx = indexFor(key, bucketCount);
        Entry* curr = buckets[idx];
        while (curr) {
            if (curr->key == key) return &curr->value;
            curr = curr->next;
        }
        return nullptr;
    }

    bool contains(const K& key) const {
        size_t idx = indexFor(key, bucketCount);
        Entry* curr = buckets[idx];
        while (curr) {
            if (curr->key == key) return true;
            curr = curr->next;
        }
        return false;
    }

    bool remove(const K& key) {
        size_t idx = indexFor(key, bucketCount);
        Entry* curr = buckets[idx];
        Entry* prev = nullptr;
        while (curr) {
            if (curr->key == key) {
                if (prev) prev->next = curr->next; else buckets[idx] = curr->next;
                delete curr;
                entryCount--;
                return true;
            }
            prev = curr;
            curr = curr->next;
        }
        return false;
    }

    vector<pair<K, V>> entries() const {
        vector<pair<K, V>> out;
        out.reserve(entryCount);
        for (size_t i = 0; i < bucketCount; i++) {
            Entry* curr = buckets[i];
            while (curr) { out.push_back({curr->key, curr->value}); curr = curr->next; }
        }
        return out;
    }

    void clear() {
        for (size_t i = 0; i < bucketCount; i++) {
            Entry* curr = buckets[i];
            while (curr) { Entry* nxt = curr->next; delete curr; curr = nxt; }
            buckets[i] = nullptr;
        }
        entryCount = 0;
    }

    size_t size() const { return entryCount; }
    bool empty() const { return entryCount == 0; }
};

// ============================================================================
// 3. CUSTOM BINARY MAX-HEAP
// ============================================================================
template <typename T, typename Compare>
class Heap {
    vector<T> data;
    Compare cmp; // cmp(a,b) == true  =>  b has higher priority than a

    void siftUp(int idx) {
        while (idx > 0) {
            int parent = (idx - 1) / 2;
            if (cmp(data[parent], data[idx])) { swap(data[parent], data[idx]); idx = parent; }
            else break;
        }
    }
    void siftDown(int idx) {
        int n = (int)data.size();
        while (true) {
            int left = 2 * idx + 1, right = 2 * idx + 2, largest = idx;
            if (left < n && cmp(data[largest], data[left])) largest = left;
            if (right < n && cmp(data[largest], data[right])) largest = right;
            if (largest == idx) break;
            swap(data[idx], data[largest]);
            idx = largest;
        }
    }
public:
    void push(const T& v) { data.push_back(v); siftUp((int)data.size() - 1); }
    T top() const { return data.front(); }
    void pop() {
        data[0] = data.back();
        data.pop_back();
        if (!data.empty()) siftDown(0);
    }
    bool empty() const { return data.empty(); }
    size_t size() const { return data.size(); }
};

// ============================================================================
// CUSTOM TREE NODE  (directory / file hierarchy, like a Git tree object)
// ============================================================================
struct FileNode {
    string name;
    bool isDirectory;
    string contentHash;     // empty for directories; points into the blob store
    long timestamp;         // last-modified logical time (for "recent activity")
    FileNode* parent;
    HashMap<string, FileNode*> children; // only meaningful if isDirectory

    FileNode(const string& n, bool dir, FileNode* p = nullptr)
        : name(n), isDirectory(dir), contentHash(""), timestamp(0), parent(p) {}

    ~FileNode() {
        // Recursively free owned children. Each tree (working tree, or any
        // commit's snapshot tree) is an independent, non-shared structure,
        // so this never causes a double-free across trees.
        auto kids = children.entries();
        for (auto& kv : kids) delete kv.second;
    }
};

// ============================================================================
// COMMIT (immutable snapshot)
// ============================================================================
struct Commit {
    string commitId;
    string message;
    string parentId;       // "" for the root commit
    string treeRootHash;   // Merkle hash of the whole tree at commit time
    string branchName;     // branch this commit was made on
    FileNode* snapshotRoot;
    long timestamp;
    int commitNumber;

    Commit() : snapshotRoot(nullptr), timestamp(0), commitNumber(0) {}
};

// Max-heap comparators ordering commits / files by recency (timestamp).
struct CommitTimeComparator {
    bool operator()(const Commit* a, const Commit* b) const { return a->timestamp < b->timestamp; }
};

struct FileActivityEntry {
    string path;
    FileNode* node;
};
struct FileActivityComparator {
    bool operator()(const FileActivityEntry& a, const FileActivityEntry& b) const {
        return a.node->timestamp < b.node->timestamp;
    }
};

// ============================================================================
// VERSION CONTROL SYSTEM
// ============================================================================
class VersionControlSystem {
private:
    HashMap<string, string>  blobStore;    // contentHash -> file content (deduplicated)
    HashMap<string, Commit*> commitStore;  // commitId    -> Commit*
    HashMap<string, string>  branches;     // branchName  -> HEAD commitId ("" = no commits)
    HashMap<string, FileNode*> pathIndex;  // full path   -> FileNode* (O(1) lookup cache)

    FileNode* workingRoot;
    string currentBranch;
    int commitCounter;
    long logicalClock;

    // ---- internal helpers ----
    static string hashString(const string& data) {
        unsigned long long hash = 14695981039346656037ULL;
        for (unsigned char c : data) { hash ^= c; hash *= 1099511628211ULL; }
        stringstream ss;
        ss << hex << setw(16) << setfill('0') << hash;
        return ss.str();
    }

    string computeTreeHash(FileNode* node) const {
        if (!node) return "";
        string combined = node->name + (node->isDirectory ? "D" : "F");
        if (node->isDirectory) {
            auto kids = node->children.entries();
            sort(kids.begin(), kids.end(), [](auto& a, auto& b) { return a.first < b.first; });
            for (auto& kv : kids) combined += kv.first + computeTreeHash(kv.second);
        } else {
            combined += node->contentHash;
        }
        return hashString(combined);
    }

    FileNode* deepCopyTree(FileNode* node, FileNode* parent) {
        if (!node) return nullptr;
        FileNode* copy = new FileNode(node->name, node->isDirectory, parent);
        copy->contentHash = node->contentHash;
        copy->timestamp = node->timestamp;
        if (node->isDirectory) {
            auto kids = node->children.entries();
            for (auto& kv : kids) copy->children.put(kv.first, deepCopyTree(kv.second, copy));
        }
        return copy;
    }

    void rebuildPathIndex(FileNode* node, const string& path) {
        if (!node->isDirectory) { pathIndex.put(path, node); return; }
        auto kids = node->children.entries();
        for (auto& kv : kids) {
            string childPath = path.empty() ? kv.first : path + "/" + kv.first;
            rebuildPathIndex(kv.second, childPath);
        }
    }

    void flattenTree(FileNode* node, const string& path, HashMap<string, string>& out) const {
        if (!node->isDirectory) { out.put(path, node->contentHash); return; }
        auto kids = node->children.entries();
        for (auto& kv : kids) {
            string childPath = path.empty() ? kv.first : path + "/" + kv.first;
            flattenTree(kv.second, childPath, out);
        }
    }

    static vector<string> splitPath(const string& path) {
        vector<string> parts;
        stringstream ss(path);
        string item;
        while (getline(ss, item, '/')) if (!item.empty()) parts.push_back(item);
        return parts;
    }

    void printTreeRecursive(FileNode* node, const string& prefix, bool isLast) const {
        if (node != workingRoot) {
            cout << prefix << (isLast ? "+-- " : "|-- ") << node->name << (node->isDirectory ? "/" : "") << "\n";
        } else {
            cout << ".\n";
        }
        if (node->isDirectory) {
            auto kids = node->children.entries();
            sort(kids.begin(), kids.end(), [](auto& a, auto& b) { return a.first < b.first; });
            for (size_t i = 0; i < kids.size(); i++) {
                string childPrefix = prefix + (node == workingRoot ? "" : (isLast ? "    " : "|   "));
                printTreeRecursive(kids[i].second, childPrefix, i == kids.size() - 1);
            }
        }
    }

    static string shortId(const string& id) { return id.empty() ? "<none>" : id.substr(0, 8); }

public:
    VersionControlSystem() : currentBranch("main"), commitCounter(0), logicalClock(0) {
        workingRoot = new FileNode("root", true, nullptr);
        branches.put("main", "");
        cout << "Initialized empty repository on branch 'main'.\n";
    }

    ~VersionControlSystem() {
        delete workingRoot;
        auto commits = commitStore.entries();
        for (auto& kv : commits) { delete kv.second->snapshotRoot; delete kv.second; }
    }

    // ---------------- working tree mutation ----------------
    void addOrUpdateFile(const string& path, const string& content) {
        auto parts = splitPath(path);
        if (parts.empty()) { cout << "Invalid path.\n"; return; }

        FileNode* curr = workingRoot;
        for (size_t i = 0; i + 1 < parts.size(); i++) {
            FileNode** found = curr->children.getPtr(parts[i]);
            FileNode* next;
            if (found) {
                next = *found;
                if (!next->isDirectory) { cout << "Error: '" << parts[i] << "' is a file, not a directory.\n"; return; }
            } else {
                next = new FileNode(parts[i], true, curr);
                curr->children.put(parts[i], next);
            }
            curr = next;
        }

        const string& fileName = parts.back();
        string cHash = hashString(content);
        if (!blobStore.contains(cHash)) blobStore.put(cHash, content); // dedup, like a Git blob

        FileNode** existing = curr->children.getPtr(fileName);
        FileNode* fileNode;
        if (existing) { fileNode = *existing; }
        else { fileNode = new FileNode(fileName, false, curr); curr->children.put(fileName, fileNode); }
        fileNode->contentHash = cHash;
        fileNode->timestamp = ++logicalClock;

        pathIndex.put(path, fileNode);
        cout << "Added/Updated: " << path << "  (blob " << cHash.substr(0, 8) << ")\n";
    }

    bool removeFile(const string& path) {
        auto parts = splitPath(path);
        if (parts.empty()) return false;
        FileNode* curr = workingRoot;
        for (size_t i = 0; i + 1 < parts.size(); i++) {
            FileNode** found = curr->children.getPtr(parts[i]);
            if (!found) return false;
            curr = *found;
        }
        const string& fileName = parts.back();
        FileNode** target = curr->children.getPtr(fileName);
        if (!target) return false;
        FileNode* node = *target;
        curr->children.remove(fileName);
        pathIndex.remove(path);
        delete node;
        return true;
    }

    // ---------------- commit / branch / rollback ----------------
    string commit(const string& message) {
        string newTreeHash = computeTreeHash(workingRoot);
        string parentId;
        branches.get(currentBranch, parentId);

        if (!parentId.empty()) {
            Commit* parentCommit = nullptr;
            commitStore.get(parentId, parentCommit);
            if (parentCommit && parentCommit->treeRootHash == newTreeHash) {
                cout << "Nothing to commit; working tree unchanged since last commit.\n";
                return "";
            }
        }

        Commit* c = new Commit();
        c->message = message;
        c->parentId = parentId;
        c->treeRootHash = newTreeHash;
        c->snapshotRoot = deepCopyTree(workingRoot, nullptr);
        c->timestamp = ++logicalClock;
        c->branchName = currentBranch;
        c->commitNumber = ++commitCounter;

        stringstream idInput;
        idInput << newTreeHash << parentId << message << c->timestamp << c->commitNumber;
        c->commitId = hashString(idInput.str());

        commitStore.put(c->commitId, c);
        branches.put(currentBranch, c->commitId);

        cout << "Committed [" << shortId(c->commitId) << "] on '" << currentBranch << "': " << message << "\n";
        return c->commitId;
    }

    bool createBranch(const string& branchName) {
        if (branches.contains(branchName)) { cout << "Branch '" << branchName << "' already exists.\n"; return false; }
        string headId;
        branches.get(currentBranch, headId);
        branches.put(branchName, headId);
        cout << "Created branch '" << branchName << "' at commit " << shortId(headId) << "\n";
        return true;
    }

    bool checkout(const string& branchName) {
        string headId;
        if (!branches.get(branchName, headId)) { cout << "Branch '" << branchName << "' does not exist.\n"; return false; }

        // Warn about uncommitted changes before discarding them.
        string currHead;
        branches.get(currentBranch, currHead);
        string currHash = computeTreeHash(workingRoot);
        Commit* currCommit = nullptr;
        bool dirty = true;
        if (!currHead.empty() && commitStore.get(currHead, currCommit) && currCommit)
            dirty = (currCommit->treeRootHash != currHash);
        else if (currHead.empty() && workingRoot->children.empty())
            dirty = false;
        if (dirty) cout << "Warning: uncommitted changes on '" << currentBranch << "' will be discarded.\n";

        currentBranch = branchName;
        delete workingRoot;
        if (!headId.empty()) {
            Commit* c = nullptr;
            commitStore.get(headId, c);
            workingRoot = c ? deepCopyTree(c->snapshotRoot, nullptr) : new FileNode("root", true, nullptr);
        } else {
            workingRoot = new FileNode("root", true, nullptr);
        }
        pathIndex.clear();
        rebuildPathIndex(workingRoot, "");
        cout << "Switched to branch '" << branchName << "'\n";
        return true;
    }

    bool rollback(const string& commitIdPrefix) {
        Commit* target = nullptr;
        string fullId;
        if (commitStore.get(commitIdPrefix, target)) {
            fullId = commitIdPrefix;
        } else {
            auto all = commitStore.entries();
            for (auto& kv : all) {
                if (kv.first.rfind(commitIdPrefix, 0) == 0) { fullId = kv.first; target = kv.second; break; }
            }
        }
        if (!target) { cout << "Commit '" << commitIdPrefix << "' not found.\n"; return false; }

        delete workingRoot;
        workingRoot = deepCopyTree(target->snapshotRoot, nullptr);
        pathIndex.clear();
        rebuildPathIndex(workingRoot, "");
        branches.put(currentBranch, fullId);

        cout << "Rolled back '" << currentBranch << "' to [" << shortId(fullId) << "]: " << target->message << "\n";
        return true;
    }

    // ---------------- historical inspection (Heap-driven) ----------------
    void log() const {
        string headId;
        branches.get(currentBranch, headId);
        if (headId.empty()) { cout << "No commits yet on '" << currentBranch << "'.\n"; return; }

        Heap<Commit*, CommitTimeComparator> heap;
        HashMap<string, bool> visited;
        Commit* head = nullptr;
        commitStore.get(headId, head);
        if (head) { heap.push(head); visited.put(headId, true); }

        cout << "Commit history for branch '" << currentBranch << "' (most recent first):\n";
        while (!heap.empty()) {
            Commit* c = heap.top(); heap.pop();
            cout << "  * [" << shortId(c->commitId) << "] t=" << c->timestamp << "  " << c->message << "\n";
            if (!c->parentId.empty() && !visited.contains(c->parentId)) {
                Commit* p = nullptr;
                if (commitStore.get(c->parentId, p) && p) { heap.push(p); visited.put(c->parentId, true); }
            }
        }
    }

    // Chronologically merges history across ALL branches using the heap --
    // this is the classic priority-queue commit-graph traversal pattern.
    void logAll() const {
        Heap<Commit*, CommitTimeComparator> heap;
        HashMap<string, bool> visited;
        auto allBranches = branches.entries();
        for (auto& b : allBranches) {
            if (b.second.empty() || visited.contains(b.second)) continue;
            Commit* c = nullptr;
            if (commitStore.get(b.second, c) && c) { heap.push(c); visited.put(b.second, true); }
        }
        cout << "Full repository history, all branches (chronological):\n";
        while (!heap.empty()) {
            Commit* c = heap.top(); heap.pop();
            cout << "  * [" << shortId(c->commitId) << "] (" << c->branchName << ") t=" << c->timestamp
                 << "  " << c->message << "\n";
            if (!c->parentId.empty() && !visited.contains(c->parentId)) {
                Commit* p = nullptr;
                if (commitStore.get(c->parentId, p) && p) { heap.push(p); visited.put(c->parentId, true); }
            }
        }
    }

    void showRecentActivity(int k) const {
        Heap<FileActivityEntry, FileActivityComparator> heap;
        auto all = pathIndex.entries();
        for (auto& kv : all) heap.push({kv.first, kv.second});
        cout << "Most recently modified files (top " << k << "):\n";
        int count = 0;
        while (!heap.empty() && count < k) {
            FileActivityEntry e = heap.top(); heap.pop();
            cout << "  " << e.path << "  (t=" << e.node->timestamp << ")\n";
            count++;
        }
        if (count == 0) cout << "  (no tracked files)\n";
    }

    void status() const {
        cout << "On branch '" << currentBranch << "'\n";
        string headId;
        branches.get(currentBranch, headId);
        if (headId.empty()) {
            cout << "No commits yet.\n";
        } else {
            string currHash = computeTreeHash(workingRoot);
            Commit* headCommit = nullptr;
            commitStore.get(headId, headCommit);
            if (headCommit && headCommit->treeRootHash == currHash)
                cout << "Working tree clean. (HEAD = " << shortId(headId) << ")\n";
            else
                cout << "Uncommitted changes present. (HEAD = " << shortId(headId) << ")\n";
        }
        cout << "Tracked files: " << pathIndex.size() << "   Unique blobs stored: " << blobStore.size()
             << "   Total commits: " << commitStore.size() << "\n";
    }

    void printWorkingTree() const { printTreeRecursive(workingRoot, "", true); }

    void catFile(const string& path) const {
        FileNode** found = pathIndex.getPtr(path);
        if (!found) { cout << "File not found: " << path << "\n"; return; }
        string content;
        blobStore.get((*found)->contentHash, content);
        cout << content << "\n";
    }

    void listBranches() const {
        auto all = branches.entries();
        sort(all.begin(), all.end(), [](auto& a, auto& b) { return a.first < b.first; });
        for (auto& kv : all)
            cout << (kv.first == currentBranch ? "* " : "  ") << kv.first << " -> " << shortId(kv.second) << "\n";
    }

    void diff(const string& id1, const string& id2) const {
        Commit *c1 = nullptr, *c2 = nullptr;
        commitStore.get(id1, c1);
        commitStore.get(id2, c2);
        if (!c1 || !c2) { cout << "One or both commit IDs not found (use full IDs from `log`).\n"; return; }

        HashMap<string, string> map1, map2;
        flattenTree(c1->snapshotRoot, "", map1);
        flattenTree(c2->snapshotRoot, "", map2);

        cout << "Diff [" << shortId(id1) << "] -> [" << shortId(id2) << "]\n";
        auto e1 = map1.entries();
        for (auto& kv : e1) {
            string v2;
            if (!map2.get(kv.first, v2)) cout << "  - removed:  " << kv.first << "\n";
            else if (v2 != kv.second)    cout << "  * modified: " << kv.first << "\n";
        }
        auto e2 = map2.entries();
        for (auto& kv : e2) if (!map1.contains(kv.first)) cout << "  + added:    " << kv.first << "\n";
    }
};

// ============================================================================
// CLI / REPL
// ============================================================================
static void printHelp() {
    cout <<
        "Commands:\n"
        "  add <path> <content...>     create/update a file with content\n"
        "  rm <path>                   remove a file\n"
        "  commit <message...>         snapshot the working tree\n"
        "  branch <name>                create a new branch from HEAD\n"
        "  checkout <name>              switch to a branch\n"
        "  branches                     list all branches\n"
        "  log                          show current branch history (Heap-ordered)\n"
        "  logall                       show full repo history, all branches\n"
        "  rollback <commitId>          reset current branch to a commit\n"
        "  status                       show working tree status\n"
        "  tree                         print the working directory tree\n"
        "  cat <path>                   print a tracked file's content\n"
        "  recent <k>                   show k most recently modified files\n"
        "  diff <commitId1> <commitId2> show differences between two commits\n"
        "  help                         show this help\n"
        "  exit                         quit\n";
}

static void runDemo(VersionControlSystem& vcs) {
    cout << "\n===== DEMO MODE =====\n\n";
    vcs.addOrUpdateFile("README.md", "Hello version control");
    vcs.addOrUpdateFile("src/main.cpp", "int main(){ return 0; }");
    string c1 = vcs.commit("Initial commit");
    vcs.printWorkingTree();

    vcs.createBranch("feature/login");
    vcs.checkout("feature/login");
    vcs.addOrUpdateFile("src/login.cpp", "void login(){ /* TODO */ }");
    vcs.addOrUpdateFile("README.md", "Hello version control\n\nNow with login.");
    vcs.commit("Add login feature");

    vcs.checkout("main");
    vcs.addOrUpdateFile("src/main.cpp", "int main(){ doStuff(); return 0; }");
    string c3 = vcs.commit("Update main entry point");

    cout << "\n--- log on main ---\n"; vcs.log();
    cout << "\n--- logall (chronological merge across branches via Heap) ---\n"; vcs.logAll();
    cout << "\n--- status ---\n"; vcs.status();
    cout << "\n--- recent activity (Heap by last-modified time) ---\n"; vcs.showRecentActivity(5);
    cout << "\n--- diff: initial commit vs latest main commit ---\n"; vcs.diff(c1, c3);
    cout << "\n--- branches ---\n"; vcs.listBranches();

    cout << "\n--- rolling main back to the initial commit ---\n";
    vcs.rollback(c1);
    vcs.printWorkingTree();

    cout << "\n===== END DEMO ===== (type your own commands now, or 'exit')\n\n";
}

int main(int argc, char** argv) {
    VersionControlSystem vcs;
    printHelp();

    if (argc > 1 && string(argv[1]) == "--demo") runDemo(vcs);

    cout << "\n> ";
    string line;
    while (getline(cin, line)) {
        if (line.empty()) { cout << "> "; continue; }
        stringstream ss(line);
        string cmd; ss >> cmd;

        if (cmd == "exit" || cmd == "quit") break;
        else if (cmd == "help") printHelp();
        else if (cmd == "add") {
            string path; ss >> path;
            string content, word;
            while (ss >> word) { if (!content.empty()) content += " "; content += word; }
            vcs.addOrUpdateFile(path, content);
        }
        else if (cmd == "rm") {
            string path; ss >> path;
            if (!vcs.removeFile(path)) cout << "File not found: " << path << "\n";
        }
        else if (cmd == "commit") {
            string message, word;
            while (ss >> word) { if (!message.empty()) message += " "; message += word; }
            if (message.empty()) message = "(no message)";
            vcs.commit(message);
        }
        else if (cmd == "branch") { string name; ss >> name; vcs.createBranch(name); }
        else if (cmd == "checkout") { string name; ss >> name; vcs.checkout(name); }
        else if (cmd == "branches") vcs.listBranches();
        else if (cmd == "log") vcs.log();
        else if (cmd == "logall") vcs.logAll();
        else if (cmd == "rollback") { string id; ss >> id; vcs.rollback(id); }
        else if (cmd == "status") vcs.status();
        else if (cmd == "tree") vcs.printWorkingTree();
        else if (cmd == "cat") { string path; ss >> path; vcs.catFile(path); }
        else if (cmd == "recent") { int k = 5; ss >> k; vcs.showRecentActivity(k); }
        else if (cmd == "diff") { string id1, id2; ss >> id1 >> id2; vcs.diff(id1, id2); }
        else cout << "Unknown command: '" << cmd << "' (type 'help')\n";

        cout << "\n> ";
    }
    cout << "Goodbye.\n";
    return 0;
}
