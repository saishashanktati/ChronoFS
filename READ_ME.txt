ADD_USER <username>
    Adds a new user to the network.

ADD_FRIEND <username1> <username2>
    Creates a bidirectional friendship between two existing users.

LIST_FRIENDS <username>
    Prints an alphabetically sorted list of the user's friends.

SUGGEST_FRIENDS <username> <N>
    Suggests up to N new friends who are friends-of-friends but not already friends.
    Ranked by number of mutual friends (descending). Ties are broken alphabetically.

DEGREES_OF_SEPARATION <username1> <username2>
    Prints the shortest distance (in edges) between two users.
    Prints -1 if no path exists.
    Prints nothing if either username1 or username2 doesnt exist.

ADD_POST <username> "<post content>"
    Adds a new post for the specified user.
    Post content can be either quoted or unquoted.

OUTPUT_POSTS <username> <N>
    Prints the most recent N posts of the specified user.
    If N = -1, prints all posts.

INPUT TERMINATION
    The input is terminated Ctrl + Z on Windows or Ctrl + D on Linux/Mac).
