#/bin/sh
#This script converts from abstract lines to Catrie NGram database format
#Input record: word1 word2 ... wordN ||||| year
awk -v m=0 '
	{
	for (e=1; e <= NF && $e != "|||||" ; ++e);
		for (i=1; i < e; i++)
		{
			for (j=0; j<4 && i+j < e; j++) printf("%s%s",!j?"":" ", $(i+j));
			print "\t>\t" m "\t" $(e+1)
			if (i < e-1) {
				for (j=1; j<4 && i+j < e; j++) printf("%s ",$(i+j));
				print "\t<"$i"\t" m "\t" $(e+1)
			}
		};
		m++;
	}'	$* | LC_ALL=C sort | tee ahra |
awk -v pdocid=-1 -F '\t' '
function printrec(tf,df)
{
     sep = ""
     for (year in tf) {
	     printf("%s%s:%d/%d",sep,year,tf[year],df[year])
	     sep = ","
     }
     printf("\n")
}
function report() {
		c = 0
		for (year in tf) {
			t = tf[year]
			if (c++) printf(",")
			printf("%d",year)
			if (t>1) printf(":%d",t)			
			if (year in df) for (lev=apn ; lev >=0 && t ; --lev) {
				d = df[year][lev];
				if (d)
					if (t != d)
						printf("/%d", d);
						else printf("/")
				t = d
			}
		}
		delete df
		delete tf
		printf("\n")
	}

BEGIN {
	split("",obs)
	print "%\tinterface\tCatrie DB\t1.0"
}
{
	an = split($1,a," ")
	for (i=1; i<=apn && i<=apn && a[i]==ap[i]; ++i);
	if (i <= an || i <= apn || $2 != p2) {
		report()
		for (j = i; j <= apn; j++) delete obs[j]
		back = substr($2,1,1) == "<"
		if (i <= an) {
			c = 0
			printf("%d\t", i)
			for (k = i; k <=an; ++k)
					printf("%s%s", c++? " ":"", a[k]);
			if (back) printf("\n")
		}
		if (back) printf("<\t%s",substr($2,2,length($2)-1))
		printf("\t")
		delete ap;
	
		for (t in a) ap[t]=a[t]; apn = an;
		p2 = $2
	}
	docid = $3
	year = $4
	
	if (!back) for (lev = 0; lev <= an; ++lev) if (!obs[lev][docid]++) df[year][lev]++;
	tf[year]++;
}
END { 
	report()
}'

