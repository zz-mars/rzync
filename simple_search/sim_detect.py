#!/usr/bin/python

INDEX_DIR = "IndexFiles.index"

import sys,os,lucene

from java.io import File
from org.apache.lucene.analysis.standard import StandardAnalyzer
from org.apache.lucene.index import DirectoryReader
from org.apache.lucene.queryparser.classic import QueryParser
from org.apache.lucene.store import SimpleFSDirectory
from org.apache.lucene.search import IndexSearcher
from org.apache.lucene.util import Version

"""
This script is loosely based on the Lucene demo class
org.apache.lucene.demo.SearchFiles. It will prompt for
a search query, then it will search the Lucene index in
the current directory called 'index' for the search
query entered against the 'contents' field. It will then 
display the 'path' and 'name' fields for each of the hits
it finds in the index. Note that saech.close() is currently
commented out because it causes a stack overflow in some
cases.
"""

if __name__ == '__main__':
	if len(sys.argv) < 2:
		print "Usage : sim_detect.py <cdc_file>"
		sys.exit(1)
	cdc_file = sys.argv[1];
	lucene.initVM(vmargs=['-Djava.awt.headless=true'])
	base_dir = os.path.dirname(os.path.abspath(sys.argv[0]))

	# directory to search
	directory = SimpleFSDirectory(File(os.path.join(base_dir,INDEX_DIR)))
	searcher = IndexSearcher(DirectoryReader.open(directory))
	analyzer = StandardAnalyzer(Version.LUCENE_CURRENT)

	f = open(cdc_file,'r')
	lines = f.readlines()
	f.close()
	dict_res = dict()
	for eachline in lines:
		query = QueryParser(Version.LUCENE_CURRENT,"contents",
				analyzer).parse(eachline)
		scoreDocs = searcher.search(query,50).scoreDocs

		for scoreDoc in scoreDocs:
			doc = searcher.doc(scoreDoc.doc)
			matched_file = doc.get('name')
			try:
				dict_res[matched_file] += 1
			except KeyError:
				dict_res[matched_file] = 1
	for k in dict_res.keys():
		print "matched file %s blocks %d\n" % (k,dict_res[k])
	del searcher

