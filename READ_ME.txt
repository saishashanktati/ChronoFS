================================================================================
IN-MEMORY GIT-INSPIRED VERSION CONTROL FILE SYSTEM
================================================================================

A single-file C++17 program that re-implements the core ideas behind Git 
(snapshots, branches, rollback, history) on top of hand-rolled data 
structures — no std::unordered_map, std::map, or std::priority_queue 
is used anywhere in the engine.

CORE CONCEPTS & DATA STRUCTURES:
- Fast key lookups       : HashMap<K,V> (separate-chaining hash table)
- Directory hierarchy    : FileNode (N-ary tree / Git-style tree object)
- Chronological history  : Heap<T,Compare> (binary max-heap)

--------------------------------------------------------------------------------
FILE LAYOUT (Top to Bottom)
--------------------------------------------------------------------------------
1. CustomHash — hash functor
2. HashMap<K,V,Hash> — custom hash table
3. Heap<T,Compare> — custom binary max-heap
4. FileNode — tree node (directory/file)
5. Commit, CommitTimeComparator, FileActivityEntry, FileActivityComparator
6. VersionControlSystem — the engine
7. printHelp, runDemo, main — CLI/REPL

================================================================================
CLASS & FUNCTION REFERENCE
================================================================================

1. CustomHash
--------------------------------------------------------------------------------
* size_t operator()(const string& key) const
  FNV-1a 64-bit hash of a string. Used as default hash functor for HashMaps.
* size_t operator()(int key) const
  Overload so HashMap can also be keyed by int if needed.


2. HashMap<K, V, Hash = CustomHash>
--------------------------------------------------------------------------------
Separate-chaining hash table with automatic rehashing (load factor > 0.75).
Used for: blob store, commit store, branch table, directory children, path index,
and visited/lookup sets.

* explicit HashMap(size_t initialCapacity = 16) : Allocates initial bucket array.
* HashMap(HashMap&&), operator=(HashMap&&) : Move-only (copy disabled).
* ~HashMap() : Frees all chain nodes via clear().
* void put(const K& key, const V& value) : Inserts/overwrites; triggers rehash().
* bool get(const K& key, V& out) const : Looks up a key; copies to out.
* V* getPtr(const K& key) const : Returns direct pointer for in-place mutation.
* bool contains(const K& key) const : true if key exists.
* bool remove(const K& key) : Removes entry; returns true if successful.
* vector<pair<K,V>> entries() const : Returns all key/value pairs.
* void clear() : Deletes all chain nodes; resets to empty.
* size_t size() const : Number of stored entries.
* bool empty() const : true if map is empty.
* [Private] size_t indexFor(const K& key, size_t mod) const : Computes bucket.
* [Private] void rehash() : Doubles bucket count and re-links entries.


3. Heap<T, Compare>
--------------------------------------------------------------------------------
Binary max-heap backed by vector<T>. Used for chronological history traversal.

* void push(const T& v) : Appends value and sifts up.
* T top() const : Returns highest-priority element.
* void pop() : Removes top element, moves last to root, sifts down.
* bool empty() const : true if heap has no elements.
* size_t size() const : Number of elements.
* [Private] void siftUp(int idx) : Bubbles element up.
* [Private] void siftDown(int idx) : Bubbles element down.


4. FileNode (The Tree)
--------------------------------------------------------------------------------
Represents a file or directory. Content is not stored inline (uses contentHash
pointing to the shared blob store).

Members:
* string name : File or directory name.
* bool isDirectory : Distinguishes file vs. directory.
* string contentHash : Key into blob store (empty for directories).
* long timestamp : Logical last modified time.
* FileNode* parent : Back-pointer to containing directory.
* HashMap<string, FileNode*> children : Child nodes, keyed by name.

Functions:
* FileNode(const string& n, bool dir, FileNode* p = nullptr)
* ~FileNode() : Recursively deletes every child.


5. Commit and Comparators
--------------------------------------------------------------------------------
* Commit : Immutable snapshot (commitId, message, parentId, treeRootHash,
  branchName, snapshotRoot, timestamp, commitNumber).
* CommitTimeComparator : Orders commits by timestamp for the log heap.
* FileActivityEntry : Pairs file path with FileNode*.
* FileActivityComparator : Orders files by timestamp for recent-activity heap.


6. VersionControlSystem (The Engine)
--------------------------------------------------------------------------------
Internal State:
* HashMap<string,string> blobStore : contentHash -> file content (deduplicated).
* HashMap<string,Commit*> commitStore : commitId -> Commit*.
* HashMap<string,string> branches : branchName -> HEAD commitId.
* HashMap<string,FileNode*> pathIndex : full path -> FileNode* (O(1) cache).
* FileNode* workingRoot : Root of live, mutable working directory.
* string currentBranch : Name of checked-out branch.
* int/long commitCounter / logicalClock : Monotonic counters.

Private Helpers:
* hashString : FNV-1a hash formatting to 16-hex-digit string.
* computeTreeHash : Recursively computes Merkle hash for a subtree.
* deepCopyTree : Clones tree structure without duplicating file content.
* rebuildPathIndex : Repopulates pathIndex after checkout/rollback.
* flattenTree : Walks tree into a flat path -> contentHash map.
* splitPath : Splits "a/b/c" path string into components.
* printTreeRecursive : ASCII tree printer.
* shortId : Shortens hash to 8 characters.

Public API:
* VersionControlSystem() / ~VersionControlSystem()
* void addOrUpdateFile(const string& path, const string& content)
* bool removeFile(const string& path)
* string commit(const string& message)
* bool createBranch(const string& branchName)
* bool checkout(const string& branchName)
* bool rollback(const string& commitIdPrefix)
* void log() const
* void logAll() const
* void showRecentActivity(int k) const
* void status() const
* void printWorkingTree() const
* void catFile(const string& path) const
* void listBranches() const
* void diff(const string& id1, const string& id2) const


7. CLI / REPL
--------------------------------------------------------------------------------
* printHelp : Prints available commands.
* runDemo : Scripted walkthrough.
* main : REPL loop and command dispatcher.

Command Reference:
* add <path> <content...> : Create/update a file
* rm <path>               : Remove a file
* commit <message...>     : Snapshot the working tree
* branch <name>           : Create a branch from HEAD
* checkout <name>         : Switch branches
* branches                : List all branches
* log                     : Current branch history
* logall                  : Full repo history, all branches
* rollback <commitId>     : Reset current branch to a commit
* status                  : Working tree status
* tree                    : ASCII directory tree
* cat <path>              : Print a file's content
* recent <k>              : Top-k recently modified files
* diff <id1> <id2>        : Differences between two commits
* help                    : Show command list
* exit / quit             : Quit the REPL
