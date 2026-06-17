#include <bits/stdc++.h>
using namespace std;


string toLowerCase(string s) {
    for (char &c : s) c = tolower(c);
    return s;
}

// AVL Tree for storing posts
struct PostNode {
    int key;
    string content;
    PostNode *left, *right;
    int height;
    PostNode(int k, string c) {
        key = k;
        content = c;
        left = right = NULL;
        height = 1;
    }
};

int heightOf(PostNode *n) {
    return n ? n->height : 0;
}

int getBalance(PostNode *n) {
    return n ? heightOf(n->left) - heightOf(n->right) : 0;
}

PostNode* rightRotate(PostNode *y) {
    PostNode *x = y->left;
    PostNode *t = x->right;
    x->right = y;
    y->left = t;
    y->height = 1 + max(heightOf(y->left), heightOf(y->right));
    x->height = 1 + max(heightOf(x->left), heightOf(x->right));
    return x;
}

PostNode* leftRotate(PostNode *x) {
    PostNode *y = x->right;
    PostNode *t = y->left;
    y->left = x;
    x->right = t;
    x->height = 1 + max(heightOf(x->left), heightOf(x->right));
    y->height = 1 + max(heightOf(y->left), heightOf(y->right));
    return y;
}

PostNode* insertPost(PostNode *node, int key, string content) {
    if (!node) return new PostNode(key, content);
    if (key < node->key) node->left = insertPost(node->left, key, content);
    else node->right = insertPost(node->right, key, content);

    node->height = 1 + max(heightOf(node->left), heightOf(node->right));

    int balance = getBalance(node);
    if (balance > 1 && key < node->left->key) return rightRotate(node);
    if (balance < -1 && key > node->right->key) return leftRotate(node);
    if (balance > 1 && key > node->left->key) {
        node->left = leftRotate(node->left);
        return rightRotate(node);
    }
    if (balance < -1 && key < node->right->key) {
        node->right = rightRotate(node->right);
        return leftRotate(node);
    }
    return node;
}

void getRecentPosts(PostNode *node, vector<string> &v, int limit) {
    if (!node) return;
    if (limit != -1 && (int)v.size() >= limit) return;
    getRecentPosts(node->right, v, limit);
    if (limit == -1 || (int)v.size() < limit)
        v.push_back(node->content);
    if (limit != -1 && (int)v.size() >= limit) return;
    getRecentPosts(node->left, v, limit);
}

struct PostTree {
    PostNode *root = NULL;
    void insert(int k, string c) { root = insertPost(root, k, c); }
    vector<string> get(int n) {
        vector<string> v;
        getRecentPosts(root, v, n);
        return v;
    }
};

// User structure
struct User {
    string name;
    string key;
    unordered_set<string> friends;
    PostTree posts;
    User(string n) {
        name = n;
        key = toLowerCase(n);
    }
};

// Social network
class SocialNet {
    unordered_map<string, User*> users;
    int postCounter = 0;
public:
    void addUser(string name) {
        string k = toLowerCase(name);
        if (users.find(k) == users.end()) users[k] = new User(name);
    }

    void addFriend(string a, string b) {
        string x = toLowerCase(a), y = toLowerCase(b);
        if (x == y || users.find(x) == users.end() || users.find(y) == users.end()) return;
        users[x]->friends.insert(y);
        users[y]->friends.insert(x);
    }

    void listFriends(string name) {
        string k = toLowerCase(name);
        if (users.find(k) == users.end()) return;
        vector<string> f;
        for (auto &p : users[k]->friends)
            f.push_back(users[p]->name);
        sort(f.begin(), f.end(), [](string a, string b){
            return toLowerCase(a) < toLowerCase(b);
        });
        for (auto &x : f) cout << x << " ";
        cout << "\n"; 
    }

    void addPost(string name, string content) {
        string k = toLowerCase(name);
        if (users.find(k) == users.end()) return;
        users[k]->posts.insert(++postCounter, content);
    }

    void outputPosts(string name, int n) {
        string k = toLowerCase(name);
        if (users.find(k) == users.end()) return;
        auto v = users[k]->posts.get(n);
        for (auto &p : v) cout << p << "\n";
    }

    void suggestFriends(string name, int n) {
        string k = toLowerCase(name);
        if (users.find(k) == users.end() || n == 0) return;
        User *u = users[k];
        unordered_map<string,int> mutual;
        for (auto &f : u->friends) {
            for (auto &ff : users[f]->friends) {
                if (ff == k) continue;
                if (u->friends.count(ff)) continue;
                mutual[ff]++;
            }
        }
        vector<pair<string,int>> list(mutual.begin(), mutual.end());
        sort(list.begin(), list.end(), [&](auto &a, auto &b){
            if (a.second != b.second) return a.second > b.second;
            return toLowerCase(users[a.first]->name) < toLowerCase(users[b.first]->name);
        });
        int count = 0;
        for (auto &x : list) {
            if (count == n) break;
            cout << users[x.first]->name << "\n";
            count++;
        }
    }

    void degrees(string a, string b) {
        string x = toLowerCase(a), y = toLowerCase(b);
        if (users.find(x) == users.end() || users.find(y) == users.end()) {
            cout << -1 << "\n";
            return;
        }
        if (x == y) { cout << 0 << "\n"; return; }
        queue<string> q;
        unordered_map<string,int> d;
        q.push(x); d[x] = 0;
        while (!q.empty()) {
            string cur = q.front(); q.pop();
            for (auto &tmkc : users[cur]->friends) {
                if (!d.count(tmkc)) {
                    d[tmkc] = d[cur] + 1;
                    if (tmkc == y) {
                        cout << d[tmkc] << "\n";
                        return;
                    }
                    q.push(tmkc);
                }
            }
        }
        cout << -1 << "\n";
    }
};

int main() {
    ios::sync_with_stdio(false);
    cin.tie(0);
    SocialNet sn;
    string line;
    while (getline(cin, line)) {
        if (line.empty()) continue;
        stringstream ss(line);
        string a, b, c;
        ss >> b;
        if (b == "ADD_USER") {
            ss >> c;
            sn.addUser(c);
        } else if (b == "ADD_FRIEND") {
            string u, v; ss >> u >> v;
            sn.addFriend(u, v);
        } else if (b == "ADD_POST") {
            string u; ss >> u;
            size_t s = line.find('"'), e = line.rfind('"');
            string post = "";
            if (s != string::npos && e != string::npos && e > s)
                post = line.substr(s + 1, e - s - 1);
            sn.addPost(u, post);
        }
        else if (b == "LIST_FRIENDS") {
            ss >> c;
            sn.listFriends(c);
        } else if (b == "OUTPUT_POSTS") {
            ss >> c;
            int n; ss >> n;
            sn.outputPosts(c, n);
        } else if (b == "SUGGEST_FRIENDS") {
            ss >> c;
            int n; ss >> n;
            sn.suggestFriends(c, n);
        } else if (b == "DEGREES_OF_SEPARATION") {
            ss >> c >> a;
            sn.degrees(c, a);
        }
    }
}
