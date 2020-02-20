#include <unistd.h> 
#include <stdio.h> 
#include <sys/socket.h> 
#include <stdlib.h> 
#include <netinet/in.h> 
#include <string.h> 
//#define PORT 8080 
#include <map>
#include <set>
#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_set>
typedef short int CatKey; // year
struct CatValue {
	int tf;
	int idf;
	CatValue() : tf(0), idf(0) {}
	CatValue(int _tf, int _idf) : tf(_tf), idf(_idf) {}
	CatValue & operator += (const CatValue& other) {
		tf +=  other.tf;
		idf += other.idf;
	}
};
std::ostream& operator << (std::ostream& ostr, const CatValue& x) {
	return ostr<<"\"tf\": " << x.tf << ", \"df\": " << x.idf;}

template<class K, class V> struct FlexibleMap {
//either sparse or dense.. but currently a mere std::map
	std::map<K,V> m;
		int total_entries() {
			return m.size();
		}
	FlexibleMap<K,V>& increment(const K& k, const V& v) {
		m[k]+=v;
		return *this;
	}
	FlexibleMap<K,V>& operator += (const std::pair<K,V> &kv) {
		return increment(kv.first, kv.second);
	}
	
};

typedef FlexibleMap<CatKey,CatValue> CatMap;
typedef std::pair<CatKey,CatValue> CatRec;

template <class K, class V>
class Trie {
	struct Node {
		std::map<K,Node> children;
		V value;
		int total_entries() {
			int rc = 0;
			for (auto & child : children)
				rc += child.second.total_entries();
			return rc + 1; // value
		}
	};
	Node root;
public:
	template <class InputIterator>
	void insert(InputIterator begin, InputIterator end, std::vector<V*>&  ng_seq /*out*/) {
		Node* n = &root;
		InputIterator i = begin;
		ng_seq.clear();
		ng_seq.push_back(&n->value);
		while (i != end) {
			n = &n->children[*i];
			ng_seq.push_back(&n->value);
			++i;
		}
	}
	template <class InputIterator, class I>
	void increment(InputIterator begin, InputIterator end, const I &incr) {
		Node* n = &root;
		InputIterator i = begin;
		while (i != end) {
			n -> value.i += incr;
			n = &n->children[*i];
			++i;
		}
		n -> value.i += incr;
	}
	template <class InputIterator>
	V* query(InputIterator begin, InputIterator end) {
		Node* n = &root;
		InputIterator i = begin;
		while (i != end && n) {
			typename std::map<K,Node>::iterator ni = n->children.find(*i);
			n = (ni == n->children.end())? 0: &ni->second;
			++i;
		}
		return n? &n->value :  0;
	}
	int  total_entries() {
		return root.total_entries();
	}
};

std::unordered_set<std::string> canonical_string;

typedef Trie<const char*,CatMap> DocTrie;

#define MAX_NGRAM (4)
size_t load_text(DocTrie &t, std::vector<const char*>& tokens, short year, size_t max_ngram) {
	std::unordered_set<CatMap*> this_doc_grams;
	for (int i=0; i<tokens.size(); ++i) {
				std::vector<CatMap*> e_seq;
				t.insert(tokens.begin()+i, tokens.begin()+std::min(i+max_ngram,tokens.size()),
					e_seq);
				for (auto *e : e_seq)
					*e += CatRec{year,CatValue{1,this_doc_grams.insert(e).second ? 1 : 0}};
	}
}

void nop(DocTrie& t, std::string filename){}

void load_text_from_file(DocTrie& t, std::string filename){
	std::ifstream ifs (filename);
	std::string str;
	char buff[80000];
	while (ifs.getline(buff,80000))
	{
		char* s = buff;
		short year = 0;
		std::vector<const char * > tokens;

		char* token=strtok(s," \t\f\r\n");
		while (token)
		{	
				if (!strcmp(token, "|||||")) {
					token = strtok(0," \t\f\n\r");
					year = atoi(token);
					break;
			}
			auto i = canonical_string.insert(token);
			tokens.push_back(i.first->c_str());
			//tokens.push_back(token);
			token=strtok(0," \t\f\r\n");
		}
		load_text(t, tokens, year, MAX_NGRAM);
	}  
}

void query (std::ostream &ostr, DocTrie& t, char *s)
{
	ostr << "{";
	std::vector<const char * >v;
	char* tok=strtok(s," \t\f\n\r") ;
	while (tok) {
		auto i = canonical_string.find(tok);
		if (i == canonical_string.end()) {
			ostr << " \"unk\": \"" << tok << "\" }\n";
			return;
		}
		else
			v.push_back(i->c_str());
		tok=strtok(0," \t\f\n\r");
	}
	CatMap* qrc = t.query(v.begin(), v.end());
	if (qrc) {
		ostr << "occ: [\n";
		for (auto &rec: qrc -> m /*temporary; needs an interface*/ )
			ostr << " { \"year\": " << rec.first << ", " << rec.second << " }\n";
		ostr << "]";
	}
	ostr << "}" << std::endl;
}

int serve_port(DocTrie& t, int port) 
{ 
    int server_fd, new_socket, valread; 
    struct sockaddr_in address; 
    int opt = 1; 
    int addrlen = sizeof(address); 
    char buffer[1024] = {0}; 
    const char *hello = "Hello from server"; 
       
    // Creating socket file descriptor 
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) 
    { 
        perror("socket failed"); 
        exit(EXIT_FAILURE); 
    } 
       
    // Forcefully attaching socket to the port 8080 
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR /*| SO_REUSEPORT */, 
                                                  &opt, sizeof(opt))) 
    { 
        perror("setsockopt"); 
        exit(EXIT_FAILURE); 
    } 
    address.sin_family = AF_INET; 
    address.sin_addr.s_addr = INADDR_ANY; 
    address.sin_port = htons( port ); 
        
    // Forcefully attaching socket to the port
    if (bind(server_fd, (struct sockaddr *)&address,  
                                 sizeof(address))<0) 
    { 
        perror("bind failed"); 
        exit(EXIT_FAILURE); 
    } 
    if (listen(server_fd, 3) < 0) 
    { 
        perror("listen"); 
        exit(EXIT_FAILURE); 
    } 
    while ((new_socket = accept(server_fd, (struct sockaddr *)&address,  
                       (socklen_t*)&addrlen))>=0) 
    { 
	    valread = read( new_socket , buffer, 1024);
	    buffer[valread]='\0'; 
	    char* http_body = strstr( buffer , "\r\n\r\n");
	    if(http_body) 
    		http_body += 4; /* move ahead 4 chars
			/* now httpbody  has just data, stripped down http headers */
		else
			http_body = buffer; /* Shouldn't happen */
	//  printf("%s\n",buffer ); 
	    std::stringstream response_hdr, response;
	    query(response,t,http_body);
	    std::string response_str(response.str());
	    response_hdr << "HTTP/1.0 200 OK\r\n"
	    << "Content-type: text/html; charset=UTF-8\r\n"
		<< "Content-Length: " << response_str.length() <<"\r\n\r\n";
		std::string response_hdr_str(response_hdr.str());

	    send(new_socket , response_hdr_str.c_str() , response_hdr_str.length() , 0); 
	    send(new_socket , response_str.c_str() , response_str.length() , 0); 
	    close (new_socket);
	//  printf("Response sent: %s\n" , response_str.c_str());
	} 
    perror("accept"); 
    return 0; 
} 

const char *arg_value(int& i, char** argv, int argc)
{
	if (argv[i][2]) return argv[i]+2; // For ex., -p8080
	else {
		i++;
		if (i >= argc) {
			std::cerr << "Error: " << argv[i-1] << " command line argument value is missing" << std::endl;
			exit(2);
		}
		return argv[i];
	}
}

int main (int argc, char** argv) {
	DocTrie t;
	int port = -1;
	for (int i=1; i<argc; i++)
		if (argv[i][0]!='-')
			load_text_from_file (t,argv[i]);
		else switch (argv[i][1]) {
			case 'p': port=atoi(arg_value(i, argv, argc)); break;
		}
	std::cerr << "Read " << t.total_entries() << std::endl;
	char buff[1024];
	if (port >= 0) {
		serve_port(t, port);
		return 0;
	}
	//Default console mode
	while (char* s = fgets(buff, 1024, stdin))
		query(std::cout, t, s);
	return 0;
}	

