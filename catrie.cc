// Catrie, a trie-based categorized n-gram TF-IDF search service
// Author: Andrei (Andreas) Scherbakov (andreas@softwareengineer.pro)

#include <unistd.h> 
#include <stdio.h> 
#include <sys/socket.h> 
#include <stdlib.h> 
#include <netinet/in.h> 
#include <string.h> 
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

typedef unsigned int CatKey; // years ago
struct CatValue {
	int tf;
	int df;
	CatValue() : tf(0), df(0) {}
	CatValue(int _tf, int _df) : tf(_tf), df(_df) {}
	CatValue & operator += (const CatValue& other) {
		tf +=  other.tf;
		df += other.df;
	}
	struct CompactAdapter {
		bool writable(const CatValue& x) {return x.tf;}
		bool want_produce(const CatValue& x, int i) {
			return i < ((x.tf <= 1) ? 0: ((x.df != x.tf)? 2 : 1) );
		}
		unsigned long produce(const CatValue& x, int i) {
			return (i > 0)? x.df: x.tf;
		}
		bool can_set(const CatValue& x, int i, int v) {
			return i <= 1 && (i == 0 ||  x.tf != 1);
		}			
		void set_done(CatValue& x, int n) {
			if (n < 1) x.tf = 1;
			if (n < 2) x.df = x.tf;
		}
		void set(CatValue& x, int i, unsigned long v) {
			(i? x.df : x.tf) = v;
		}
		typedef unsigned int itype;
	};
};

	struct TFCompactAdapter {
		bool writable(const unsigned int& x) {return x;}
		bool want_produce(const unsigned int& x, int i) {return i < ((x <= 1)? 0: 1);}
		unsigned long produce(const unsigned int& x, int _i) {
			return x;
		}
		void set_done(unsigned int& x, int n) {
			if (n < 1) x = 1;
		}		
		bool can_set(unsigned int& x, int i, unsigned int v) {
			return i<1;
		}
		void set(unsigned int& x, int i, unsigned int v) {
			x = v;
		}
		typedef unsigned int itype;
	};

std::ostream& operator << (std::ostream& ostr, const CatValue& x) {
	return ostr<<"\"tf\": " << x.tf << ", \"df\": " << x.df;}

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
typedef CompactUI2UIMap<unsigned int,CatValue, CatValue::CompactAdapter> CatMap;
typedef CompactUI2UIMap<unsigned int,unsigned int, TFCompactAdapter> LookbehindMap;
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
	CacheControl(/*const U& _dispose, */size_t _capacity)
		:/*dispose(_dispose), */curr_stamp(0U), capacity(_capacity) {}
};

size_t trie_cache_capacity = 40000;
size_t keep_level_occ_num_thereshold = 40000;

template <class K, class V>
struct Trie {
	struct LookbehindNode {
		K token;
		LookbehindMap tf;
	};
	struct Node {
		//std::map<K,Node> children;
		long int start_pos;
		Node *children;
		int n_children;
		LookbehindNode *prefix_tf;
		int n_prefix_tf;
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
		Node(long int _start_pos, K k) : start_pos(_start_pos),token(k), value(), children(0), n_children(0), prefix_tf(0), n_prefix_tf(0) {}
		Node(Node&& other) {
			prefix_tf = other.prefix_tf;
			other.prefix_tf = 0;
			children = other.children;
			other.children = 0;
			n_prefix_tf = other.n_prefix_tf;
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
	void cache_children(Node* n, int level) {	
			if ((!n->children && n->n_children || !n->prefix_tf && n->n_prefix_tf)
				// needs download?
				&& cache.insert(n))
					//2 a more generic way!
					{
						load_from_sorted_structured_occ_file(ng_file, n, level);
					}
	}

	template <class InputIterator>
	Node* query(InputIterator begin, InputIterator end) {
		Node* n = &root;
		InputIterator i = begin;
		while (i != end && n) {
			cache_children(n, i-begin);
			if (!n->children) return 0;
			Node qn(0U,*i);
			auto ni = std::lower_bound(n->children,n->children+n->n_children,
							qn, [](const Node& x, const Node& y){return strcmp(x.token,y.token) < 0;} );
			n = (ni >= n->children+n->n_children || ni->token != *i)? 0 : ni;
			++i;
		}
		return n;
	}
	int  total_entries() {
		return root.total_entries();
	}
	Trie() : root(0U, ""), cache(/* Node::uncache_content, */  trie_cache_capacity) {}
};

std::unordered_set<std::string> canonical_string;

static unsigned base_year;
static size_t occ_record_fanout_limit = 10;

typedef Trie<const char*,CatMap> DocTrie;

struct LevelScaffold {
	std::string token;
	int n_occ, n_occ_record;
	std::map<CatKey,CatValue> occurrences;
	std::map<const char*,std::map<CatKey, unsigned int> > prefix_tf;
	std::list<DocTrie::Node> children;	
	DocTrie::Node* trie_elem;

	LevelScaffold &operator << (const LevelScaffold& child) {
			n_occ_record += child.n_occ_record;
			n_occ += child.n_occ;
			for (auto &yo: child.occurrences)
				//only propagate tf; df should be already counted at all levels
				occurrences.insert(std::pair<CatKey,CatValue>(yo.first,{})).first->second.tf+=yo.second.tf;
			for (auto &pref: child.prefix_tf)
			for (auto &yo: pref.second)
				prefix_tf[pref.first].insert(std::pair<CatKey, unsigned int>(yo.first,0U)).first->second+=yo.second;
		return *this;
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
		trie_elem->n_prefix_tf = prefix_tf.size();
		if (!dispose && trie_elem->n_prefix_tf)
		{
			trie_elem->	prefix_tf = (DocTrie::LookbehindNode*) malloc(trie_elem->n_prefix_tf * sizeof(DocTrie::LookbehindNode) );
			int i = 0;
			for (auto& e: prefix_tf) {
				auto& rec = trie_elem->prefix_tf[i++];
				new(&rec) DocTrie::LookbehindNode();
				rec.token = e.first;
				rec.tf = LookbehindMap(e.second);
			}
		}
		return *this;
	}
	LevelScaffold(const char* _token, DocTrie::Node* _trie_elem)
		:	token(_token), trie_elem(_trie_elem),
			n_occ(0), n_occ_record(0) {}
};
	
static void read_occurrences(char* & s, std::vector<LevelScaffold>& stack)
	{
		do
		{
		char* e; //->pos_t !!
		int year = std::strtol(s,&e,0);
		s=e;
		long f = 1L, df = 1L;
		if (*s == ':') {
			s++;
			f = std::strtol(s,&e,0);
			if (s==e) f = 1;
			s=e;
	}
		if (*s == '/') {
				s++;
				df = std::strtol(s,&e,0);
				if (s==e) df = f;
				s=e;
			}
			//assert(!stack.empty());
			int i = stack.size()-1;
			auto &r = stack[i].occurrences.insert(std::pair<CatKey,CatValue>(base_year-year,{})).first->second;
			r.df+=df; r.tf+=f;
			f = df;
			while (*s == '/') {
				s++;
				df = std::strtol(s,&e,0);
				if (s==e) df = f;
				s=e;
				if (i-- < 1) {
					//static int n_ex_n_level = 0;
					//if (!n_ex_n_level++) std::cerr<<("DB format warning: n-gram level out of bounds (too many slashes)"); 
				} else {
				auto &r = stack[i].occurrences.insert(std::pair<CatKey,CatValue>(base_year-year,{})).first->second;
				r.df += df;
				f = df;
				}
			}
//		} else {
//					static int n_ex_occurrences = 0;
//					if (!n_ex_occurrences++) std::cerr<<("DB format error: tf/idf is missing (a semicolon is expected)"); 
//		}
	}
	while (*s == ',' && *++s);
}

static void read_occurrences(char* & s, std::map<CatKey,unsigned int>& fmap)
{
	do
	{
		char* e; //->pos_t !!
		int year = std::strtol(s,&e,0);
		s=e;
		long f = 1L;
		if (*s == ':') {
			s++;
			f = std::strtol(s,&e,0);
			if (s==e) f = 1;
			s=e;
		}
		fmap.insert(std::pair<CatKey,unsigned>(base_year-year,0U)).first->second+=f;
	}
	while (*s == ',' && *++s);
}

void load_from_sorted_structured_occ_file(FILE* f,
		DocTrie::Node * root, int root_level)
	{
			std::cerr << "Load from pos: " << root->start_pos << ", tree level: " << root_level << std::endl;
			std::vector<LevelScaffold> stack;
			stack.emplace_back("", root);
			
			long int  start_pos = root -> start_pos;
			fseek(f,start_pos, SEEK_SET);
			char buff[4096];
			short term_read = 0;
			//add an empty string terminator to ensure all levels are finally closed
			while(char *s = fgets(buff, 4096, f) ? buff : (term_read++? 0 : &(buff[0]='\0')))
			{
			char* tp = strchr(s,'\t');
			char rectype = '#';
			int year = -1;
			if (tp) {
				while (*s == ' ') ++s;
				rectype = s[0];
				s=tp + 1;
			}
			char *tail = 0;
			tp = strchr(s,'\t');
			if (tp) {
				tail = tp + 1;
				*tp = 0;
			}
			if (rectype > '0' && rectype <= '9') {
			char * tok = strtok(s," \t\f\n\r");
				int i = rectype - '0' - root_level;
			//first time simply ignore "underground" levels
			if (i < 1 && stack.size() <= 1)
			  while (i < 1 && tok) {tok = strtok(0," \t\f\n\r"); i++;}
			for (; stack.size() > std::max(i,1); stack.pop_back())
				//Merge finished levels into their ancestors
				stack[stack.size()-2] << std::move(stack.back().store(root_level?0:keep_level_occ_num_thereshold));
			if (i < 1) break;
			for (; tok; tok = strtok(0," \t\f\n\r")) {
				auto &siblings = stack.back().children;
				auto i = canonical_string.insert(tok);
				siblings.emplace_back( start_pos,i.first -> c_str() );
				stack.emplace_back(LevelScaffold(tok,&siblings.back()));
			}
			if (tail) read_occurrences(tail, stack );
			start_pos = ftell(f);
		}
		else if (rectype == '<') {
			char* tok = strtok(s," \t\f\n\r");
					if (tail) read_occurrences(tail, stack.back().prefix_tf[canonical_string.insert(tok).first
 -> c_str() ]);
					if (strtok(0," \t\f\n\r")) {
						static int n_lookbehind_extra_token = 0;
						std::cerr << "Catrie DB error: extra_token in lookbehind" << std::endl;
					}
				}
		}
		stack.front().store(0);
}

std::ostream& print_tf_idf(std::ostream& ostr, const CatMap& freq_map) {
		typedef std::vector<std::pair<unsigned int,CatValue > > CatVec;
		CatVec v;
		freq_map.copy(std::insert_iterator<CatVec>(v,v.end()));
		int j = v.size();
		for (auto &rec: v /*temporary; needs an interface*/ ) {
			ostr << " { \"year\": " << base_year-rec.first << ", \"tf\": " << rec.second.tf << ", \"df\": " << rec.second.df << " }" ;
			ostr << (--j? "," : "") << "\n";
		}
		return ostr;
	}

std::ostream& print_tf(std::ostream& ostr, const LookbehindMap& freq_map) {
		typedef std::vector<std::pair<unsigned int,unsigned int> > CatVec;
		CatVec v;
		freq_map.copy(std::insert_iterator<CatVec>(v,v.end()));
		int j = v.size();
		for (auto &rec: v /*temporary; needs an interface*/ ) {
			ostr << " { \"year\": " << base_year-rec.first << ", \"tf\": " << rec.second << " }" ;
			ostr << (--j? "," : "") << "\n";
		}
		return ostr;
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
	bool wildcard = false, prefix_wildcard = false;
	while (tok) {
		wildcard = (tok[0] == '*' && tok[1] == 0);
		if (!wildcard) {
			auto i = canonical_string.find(tok);
		if (i == canonical_string.end()) {
			ostr << " \"unk\": \"" << tok << "\" }\n";
			return;
		}
		else
			v.push_back(i->c_str());
		} else { /* Wildcard */
			if (v.empty()) prefix_wildcard = true;
		}
		tok=strtok(0," \t\f\n\r");
	}
	auto* qrc = t.query(v.begin(), v.end());
	if (qrc) {
		ostr<<"\"qlen: \"" << len << ", \"occ\": [\n";
		print_tf_idf( ostr, qrc -> value);
		ostr<<"]";
		if (wildcard) /* Write suffix TF/DF list */ {

			t.cache_children(qrc,v.size());
			ostr<<",\nsuffix: [\n";
			if (qrc->children)
				for (int c = 0; c < qrc -> n_children; ++c) {
					ostr<<"tok: \"" << qrc -> children[c].token << "\", occ: [\n";
					print_tf_idf(ostr, qrc -> children[c].value);
					ostr<<"]\n";
				}
		ostr<<"]";
		}
		if (prefix_wildcard) { /* Free prefix TF list */
			t.cache_children(qrc,v.size());
			ostr<<",\nprefix: [\n";
			if (qrc->prefix_tf)
				for (int c = 0; c < qrc -> n_prefix_tf; ++c) {
					ostr<<"tok: \"" << qrc->prefix_tf[c].token << "\", occ: [\n";
					print_tf(ostr, qrc->prefix_tf[c].tf);
					ostr<<"]\n";
				}
		ostr<<"]";
		}
		
	}
	ostr << "\n}" << std::endl;
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
			load_from_sorted_structured_occ_file(ng_file = fopen(argv[i],"rt"), &t.root, 0);
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

