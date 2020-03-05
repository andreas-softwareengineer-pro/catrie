#include <unistd.h> 
#include <stdio.h> 
#include <sys/socket.h> 
#include <stdlib.h> 
#include <netinet/in.h> 
#include <string.h> 
//#define PORT 8080 
#include <map>
#include <list>
#include <unordered_map>
#include <set>
#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <functional>
#include <algorithm>
#include "CompactUI2UIMap.h"

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

/* template<class K, class V> struct FlexibleMap {
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
*/
typedef CompactUI2UIMap<unsigned int,unsigned int> CatMap;
typedef std::pair<CatKey,CatValue> CatRec;

size_t max_cached_subtries = (1 << 15);
static FILE* ng_file;

template<class E, class U> class CacheControl
{
	std::vector<E*> cached;
	typedef unsigned long stamp_t;
	std::unordered_map<E*,stamp_t> cached_table;
	U dispose;
	stamp_t curr_stamp;
	size_t capacity;
	struct Younger {
		std::unordered_map<E*,stamp_t>& cached_table;
		bool operator()(E* x, E* y){
				return cached_table[x] > cached_table[y];
		}
	};
public:
	bool insert(E* ep) {
		auto p = cached_table.insert(std::pair<E *,stamp_t>(ep,0U));
		p.first-> second = curr_stamp++;
		if (!p.second) return false;
		if (cached.size() >= capacity) {
			std::pop_heap(cached.begin(),cached.end(),Younger{cached_table});
			cached.back()->uncache_content();
			cached_table.erase(cached.back());
			cached.back()=ep;
		} else {
			cached.push_back(ep);
			std::push_heap(cached.begin(),cached.end(),Younger{cached_table});
		}
	return true;	
	}
	CacheControl(const U& _dispose, size_t _capacity)
		:dispose(_dispose), curr_stamp(0U), capacity(_capacity) {}
};

size_t trie_cache_capacity = 40000;
size_t keep_level_occ_num_thereshold = 40000;

template <class K, class V>
struct Trie {
	struct Node {
		//std::map<K,Node> children;
		fpos_t start_pos;
		Node *children;
		int n_children;
		K token;
		V value;
		void uncache_content() {
			if (children) {
				for (int i=0; i<n_children; ++i) children[i].~Node();
				free(children);
				children = 0;
			}
		}
		int total_entries() {
			int rc = 0;
			if (children)
			for (int i=0; i<n_children; ++i)
				rc += children[i].total_entries();
			return rc + 1; // value
		}
		Node(fpos_t _start_pos, K k) : start_pos(_start_pos),token(k), value(), children(0), n_children(0) {}
		Node(Node&& other) {
			children = other.children;
			other.children = 0;
			n_children = other.n_children;
			token = std::move(other.token);
			value = std::move(other.value);
			start_pos = other.start_pos;
		}
		~Node() {uncache_content();}
	};
	Node root;
	CacheControl<Node,void(Node::*)()> cache;
public:
	template <class InputIterator>
	void insert(InputIterator begin, InputIterator end, std::vector<V*>&  ng_seq /*out*/) {
		Node* n = &root;
		InputIterator i = begin;
		ng_seq.clear();
		ng_seq.push_back(&n->value);
		while (i != end) {
			n = &n->palette()[*i];
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
			n = &n->palette()[*i];
			++i;
		}
		n -> value.i += incr;
	}
	template <class InputIterator>
	V* query(InputIterator begin, InputIterator end) {
		Node* n = &root;
		InputIterator i = begin;
		while (i != end && n) {
			if (!n->children && n->n_children
				// needs download?
				&& cache.insert(n))
					//2 a more generic way!
					{
						load_from_sorted_occ_file(ng_file, n, i-begin);
						if (!n->children) return 0;
					}
			Node qn(0U,*i);
			auto ni = std::lower_bound(n->children,n->children+n->n_children,
							qn, [](const Node& x, const Node& y){return strcmp(x.token,y.token) < 0;} );
			n = (ni >= n->children+n->n_children || ni->token != *i)? 0 : ni;
			++i;
		}
		return n? &n->value :  0;
	}
	int  total_entries() {
		return root.total_entries();
	}
	Trie() : root(0U, ""), cache(Node::uncache_content, trie_cache_capacity) {}
};

std::unordered_set<std::string> canonical_string;

static unsigned base_year;
static size_t occ_record_fanout_limit = 10;

typedef Trie<const char*,CatMap> DocTrie;

struct LevelScaffold {
	std::string token;
	int n_occ, n_occ_record;
	std::map<unsigned int,unsigned int> occurrences;
	std::list<DocTrie::Node> children;	
	DocTrie::Node* trie_elem;

	operator << (const LevelScaffold& child) {
			n_occ_record += child.n_occ_record;
			n_occ += child.n_occ;
			for (auto &yo: child.occurrences)
				occurrences.insert(std::pair<int,int>(yo.first,0)).first->second+=yo.second;
		}

	LevelScaffold &store(size_t level_occ_num_thereshold)
	{
		bool dispose = n_occ  < level_occ_num_thereshold;
		if (true /* temporary */ || dispose || n_occ_record >= occ_record_fanout_limit ) {
			trie_elem->value = CatMap(occurrences);
			if (!*(char**)&trie_elem -> value) abort();
			// std::cout << "St " <<   token << " " << strlen(*(char**)&trie_elem -> value) << std::endl;
			n_occ_record = 1;
		}
		trie_elem->n_children = children.size();
		if (!dispose && trie_elem->n_children)
		{
			trie_elem->	children = (DocTrie::Node*) malloc(trie_elem->n_children * sizeof(DocTrie::Node) );
			int i = 0;
			for (auto&& e: children) {
				new(trie_elem->children+(i++))DocTrie::Node(std::move(e)); 
			}
		}
		return *this;
	}
	LevelScaffold(const char* _token, DocTrie::Node* _trie_elem)
		:	token(_token), trie_elem(_trie_elem),
			n_occ(0), n_occ_record(0) {}
};
	
	void load_from_sorted_occ_file(FILE* f,
		DocTrie::Node * root, int root_level)
	{
			std::cerr << "Load from pos: " << root->start_pos << ", tree level: " << root_level << std::endl;
			LevelScaffold root_s("", root);
			std::vector<LevelScaffold> stack;
			
			fpos_t  start_pos = root -> start_pos;
			fseek(f,start_pos, SEEK_SET);
			char buff[4096];
			short term_read = 0;
			//add an empty string terminator to ensure all levels are finally closed
			while(char *s = fgets(buff, 4096, f) ? buff : (term_read++? 0 : &(buff[0]='\0')))
			{
			char* tp = strchr(s,'\t');
			int year = -1;
			if (tp) {
				year = atoi(tp+1);
				*tp = 0;
			}
			char * tok = strtok(s," \t\f\n\r");
			int i = 0;
			//fill a stub if any
			if (stack.empty())
			  for (; tok && i < root_level;
					tok=strtok(0," \t\f\n\r"), i++	)
				stack.emplace_back(LevelScaffold(tok,0));
			//skip continuing levels
			for (; tok && i<stack.size() && 
				!strcmp(tok, stack[i].token.c_str());
				tok=strtok(0," \t\f\n\r"), i++) ;
			for (; stack.size() > std::max(i,root_level); stack.pop_back())
				//Merge finished levels into their ancestors
				(stack.size()>root_level+1? stack[stack.size()-2]:root_s) << std::move(stack.back().store(root_level?0:keep_level_occ_num_thereshold));
			if (i < root_level) break;
			for (; tok; tok = strtok(0," \t\f\n\r")) {
				auto &siblings = (stack.size()>root_level?stack.back():root_s).children;
				auto i = canonical_string.insert(tok);
				siblings.emplace_back( start_pos,i.first -> c_str() );
				stack.emplace_back(LevelScaffold(tok,&siblings.back()));
				++stack.back().occurrences.insert(std::pair<unsigned int,unsigned int>(base_year-year,0)).first->second;
			}
			start_pos = ftell(f);
		}
		root_s.store(0);
}

#define MAX_NGRAM (4)
#if(0)
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
#endif

void query (std::ostream &ostr, DocTrie& t, char *s)
{
	ostr << "{";
	std::vector<const char * >v;
	int len = strlen(s);
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
		typedef std::vector<std::pair<unsigned int,unsigned int> > CatVec;
		CatVec qrcv;
		qrc->copy(std::insert_iterator<CatVec>(qrcv,qrcv.end()));
		ostr << "\"qlen\": " << len << ", \"occ\": [\n";
		int j = qrcv.size();
		for (auto &rec: qrcv /*temporary; needs an interface*/ ) {
			ostr << " { \"year\": " << base_year-rec.first << ", \"tf\": " << rec.second << " }" ;
			ostr << "\n";
		}
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
	    int thisvalread;
	    valread = 0;
	    if ((thisvalread = recv( new_socket , buffer+valread, 1024-1-valread, 0)) > 0)
	    	valread += thisvalread;
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
	time_t start_t;
	time(&start_t);
	base_year = gmtime(&start_t)->tm_year + 1900U + 1U;
	int port = -1;
	for (int i=1; i<argc; i++)
		if (argv[i][0]!='-')
			//load_text_from_file (t,argv[i]);
			load_from_sorted_occ_file(ng_file = fopen(argv[i],"rt"), &t.root, 0);
		else switch (argv[i][1]) {
			case 'p': port=atoi(arg_value(i, argv, argc)); break;
		}
	std::cerr << t.total_entries() << " trie nodes were permanently cached" << std::endl;
	char buff[1024];
	strcpy(buff, "submarine");
	std::cerr<<"Ready."<<std::endl;
	std::cerr<<"Test query: "<< buff << std::endl;
	query (std::cerr, t, buff) ;
	//exit(0);
	if (port >= 0) {
		serve_port(t, port);
		return 0;
	}
	//Default console mode
	while (char* s = fgets(buff, 1024, stdin))
		query(std::cout, t, s);
	return 0;
}	

