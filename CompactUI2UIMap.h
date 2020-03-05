/* ----------- {k->v} Map Serialization Legend -------------
Let k is sorted in an ascending order.
(k->v) where v>0 is encoded by a sequence of two optional tagged numbers:
---------------------------------------------------------
1) if v == 1 || k!=previous_k+1 || last recorded is DeltaK(..):
	    DeltaK(k-previous_k)
	for the very least k, previous_k is assumed to be -1
2) if v > 1:
		ExtraV(v-1)
Variate byte encoding used:
		0x00 - terminator
		0x01..0x7F - 7 least significant bits of DeltaK
		0x80 - reserved
		0x81..0xBF - 6 least significant bits of ExtraV
		0xC0..0xFF - number continuation, next 6 bits (a la UTF-8)
*/
#include <map>
# include <type_traits>
#include <string.h>

template <typename I>
void append_varbyte_num(I v, char* &p, bool flag)
{
		/*assert(v || flag);*/
		if (flag) {*p++ = 0x80 | (v & 0x3F); v>>=6;}
		else {*p++ = v & 0x7F; v>>=7;}
		while (v)
			{*p++ = 0xc0 | (v & 0x3F); v>>=6;}
}

template <typename I>
int get_varbyte_size(int v, bool flag)
{
		int sz(1);
		/*assert(v || flag);*/
		if (flag) {v<<=6;}
		else {v<<=7;}
		while (v)
			{++sz; v<<=7;}
		return sz;
}

template <typename I>
void read_varbyte_num(I &v, const char* &p, bool &flag) {
	flag = !!(*p & 0x80);
	int i;
	if (flag) v = *p++ & 0x3F, i=6;
	else v = *p++ & 0x7F, i=7;
	while ((*p & 0xc0) == 0xc0)
		v|=(*p++ & 0x3f) << i, i+=6;
}
 
template <typename K, typename V>
class CompactUI2UIMap {
	char *q;
public:
	CompactUI2UIMap(const  std::map<K,V> & m) {
		bool last_flag = true;
		K last_key = ~K(0U);
		size_t bufsize;
		char buffer[bufsize = (2 + 8 * (sizeof(K) + sizeof(V)) / 6) * m.size() + 1],
			*p=buffer;
		for (auto & r: m)
			if (r.second>0) {
				if (r.second == 1U || r.first!=last_key+1 || !last_flag) {
					append_varbyte_num(r.first-last_key,p,last_flag=false);
				}
				if (r.second > 1U)
					append_varbyte_num(r.second-1U, p, last_flag=true);
				last_key = r.first;
			}
		*p++ = 0;
		/*assert(p < buffer + bufsize);*/
		q = (char*)malloc(p - buffer);
		strcpy(q,buffer);
	}
	CompactUI2UIMap() : q(0) {}

	template<class InsertIterator> copy(InsertIterator ii) {
		std::pair<K, V> out_elem(~K(0U),0U);
		const char* p=q;
		bool flag;
		bool key_pending = false;
		typename std::conditional<
            (sizeof(K) <= sizeof(V)), V, K >::type n;
		if (p)
		while (*p)
		{
			read_varbyte_num(n,p,flag);
			if (flag) {
				//xtra value
				out_elem.second = n+1U;
				if (!key_pending) ++out_elem.first;
				*ii = out_elem;
			} else {
				//key increment
				if (key_pending) {
					out_elem.second = 1U;
					*ii = out_elem;
				}
				out_elem.first += n;
			}
			key_pending = !flag;
		}
		if (key_pending) {
			out_elem.second = 1U;
			*ii = out_elem;
		}
	}				
	CompactUI2UIMap & operator = (CompactUI2UIMap&& other) {
		if (q) free(q);
		q=other.q; other.q=0;
		return *this;
	}
	CompactUI2UIMap(const CompactUI2UIMap& other) {
		//string like
		free(q);
		if (other.q) {
			q=(char*)malloc(strlen(other.q))+1;
			strcpy(q,other.q);}
		else q=0;
	}
	CompactUI2UIMap(CompactUI2UIMap&& other) {
		q = 0;
		*this = *other;
		other.q = 0;
	}
	~CompactUI2UIMap() {free(q);}
};
#ifdef TEST_COMPACTUI2UIMAP
#include <stdio.h>
#include <iostream>
#include <map>
struct AssignLogger {
	template<typename K, typename V> operator = (const std::pair<K,V> &y) {
		std::cout << y.first <<" : " << y.second << std::endl;}
};
struct AssignLoggerP {
	AssignLoggerP &operator ++() {return *this;}
	AssignLogger operator *() {return AssignLogger();}
};

int main(int argc, char** argv) {
	char buff[1024];
	std::map<unsigned int, unsigned int> m;
	unsigned k,v;
	while (char *s = fgets(buff, 1024, stdin)){
		if (2==sscanf(s, "%d %d", &k, &v)) {
			m[k]=v;
			CompactUI2UIMap<unsigned int, unsigned int>(m).copy(AssignLoggerP());
		} else std::cerr<<"Use integer 'key value' pair"<< std::endl;
	}
	return 0;
}
#endif // TEST_COMPACTUI2UIMAP
