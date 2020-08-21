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
		0x01..0x7F - DeltaK (next 7 bits of DeltaK value)
		0x80 - reserved
		0x81..0xBF - ExtraV (6 least significant bits = 6 next bits of ExtraV value)
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
 
template <typename K, typename V, class Adapter>
class CompactUI2UIMap {
	char *q;
public:
	template<class V0>
	CompactUI2UIMap(const  std::map<K,V0> & m) {
		bool zero_state = true;
		K last_key = ~K(0U);
		size_t bufsize;
		Adapter ada;
		//Reconsider?
		char buffer[bufsize = 5* (2 + 8 * (sizeof(K) + sizeof(V)) / 6) * m.size() + 1],
			*p=buffer;
		for (auto & r: m)
			if (ada.writable(r.second)) {
				bool w = ada.want_produce(r.second,0);
				if (!zero_state || !w || r.first!=last_key+1) {
					append_varbyte_num(r.first-last_key,p,false);
				}
				V mirror;
				ada.init(mirror);
				int i = 0;
				zero_state = false;
				while (w) {
					auto v = ada.produce(r.second, i);
					append_varbyte_num(v, p, true);
					ada.load(mirror, i, v);
					++i;
					w = !(zero_state = ada.hard_stop(mirror, i, v)) && 
						ada.want_produce(r.second, i);
				}
				last_key = r.first;
			}
		*p++ = 0;
		/*assert(p < buffer + bufsize);*/
		q = (char*)malloc(p - buffer);
		strcpy(q,buffer);
	}
	CompactUI2UIMap() : q(0) {}

	template<class OutputIterator> void copy(OutputIterator ii) const {
		const char* p=q;
		Adapter ada;
		std::pair<K, V> elem(~K(0U),{});
		ada.init(elem.second);
		bool last_flag = true, flag;
		typename std::conditional<
			(sizeof(K) <= sizeof(typename Adapter::itype)), typename Adapter::itype, K >::type v;
		int i = 0;
		bool last_hard_stop = false;
		
		if (p) while (*p)
		{
			read_varbyte_num(v,p,flag);
			if (!flag && (i || !last_flag) || last_hard_stop)
			{
							//deliver an element
							*ii++ = elem;
							elem.second.~V(); new(&elem.second) V(); ada.init(elem.second);
							i = 0;
			}
			if (flag) {
					//Auto increment key if needed
					if (!i && last_flag) ++elem.first;
					
					//Load next value figure
					ada.load(elem.second, i++, v);
			} else {
					elem.first += v;
			}
			last_flag=flag;
			last_hard_stop = i && ada.hard_stop(elem.second,i,v);		
		}
		if (!last_flag || i) {
			//deliver the last element
			*ii++ = elem;
		}
	}				
	CompactUI2UIMap & operator = (CompactUI2UIMap&& other) {
		free(q);
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
		free(q);
		q = other.q;
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
