#!/usr/bin/python

INDEX_DIR = "IndexFiles.index"

import sys, os, lucene, threading, time
from datetime import datetime

from java.io import File
from org.apache.lucene.analysis.miscellaneous import LimitTokenCountAnalyzer
from org.apache.lucene.analysis.standard import StandardAnalyzer
from org.apache.lucene.document import Document,Field,FieldType
from org.apache.lucene.index import FieldInfo, IndexWriter, IndexWriterConfig
from org.apache.lucene.store import SimpleFSDirectory
from org.apache.lucene.util import Version

"""
This class is loosely based on the lucene demo class
org.apache.lucene.demo.IndexFiles. It will take a 
directory as an argument and will index all of the 
files in that directory and downward recursively.
It will index on the file path, the file name and the
file contents. The resulting lucene index will be placed
in current directory and called 'index'.
"""

class Ticker(object):
	def __init__(self):
		self.tick = True
	
	def run(self):
		while self.tick:
			sys.stdout.write('.')
			sys.stdout.flush()
			time.sleep(1.0)

class IndexFiles(object):
	"""Usage: python <doc_directory>"""
	# __init__ : constructor
	# @root : the documents directory
	# @storeDir : the directory in which index will be sored
	# analyzer : the analyzer..
	def __init__(self,root,storeDir,analyzer):
		# Create the index dir if it does not exist 
		if not os.path.exists(storeDir):
			os.mkdir(storeDir)
		# the SimpleFSDirectory which the index will be written in
		store = SimpleFSDirectory(File(storeDir))
		analyzer = LimitTokenCountAnalyzer(analyzer,1048576)
		config = IndexWriterConfig(Version.LUCENE_CURRENT,analyzer)
		config.setOpenMode(IndexWriterConfig.OpenMode.CREATE)
		# create a index writer 
		# atach the index dir and config info to it
		writer = IndexWriter(store,config)

		# call the indexing procedure
		# indexing all the files in the directory specified by root
		# write the index with writer
		self.indexDocs(root,writer)
		# start a ticker
		ticker = Ticker()
		print 'commit index'
		threading.Thread(target=ticker.run).start()
		writer.commit()
		writer.close()
		# stop the ticker when the indexing procedure completes
		ticker.tick = False
		print 'Done'
	def indexDocs(self,root,writer):
		t1 = FieldType()
		t1.setIndexed(True)
		t1.setStored(True)
		t1.setTokenized(True)
		t1.setIndexOptions(FieldInfo.IndexOptions.DOCS_AND_FREQS)

		t2 = FieldType()
		t2.setIndexed(True)
		t2.setStored(False)
		t2.setTokenized(True)
		t2.setIndexOptions(FieldInfo.IndexOptions.DOCS_AND_FREQS_AND_POSITIONS)

		for root, dirnames,filenames in os.walk(root):
			# traverse through the doc directory
			for filename in filenames:
				# only if this file ends with '.c'
				if not filename.endswith('.c'):
					continue
				try:
					# only add the filename and path for indexing
					path = os.path.join(root,filename)
					print "adding file : ",path
					file = open(path)
					contents = unicode(file.read(),'utf-8')
					file.close()
					doc = Document()
					doc.add(Field("name",filename,t1))
					doc.add(Field("path",root,t1))
				#	if len(contents) > 0:
				#		doc.add(Field("contents",contents,t2))
				#	else:
				#		print "warning: no content in ",filename
					writer.addDocument(doc)
				except Exception,e:
					print "failed in indexDocs:",e

if __name__ == '__main__':
	if len(sys.argv) < 2:
		print IndexFiles.__doc__
		sys.exit(1)
	lucene.initVM(vmargs=['-Djava.awt.headless=true'])
	print "lucene",lucene.VERSION
	start = datetime.now()
	try:
		base_dir = os.path.dirname(os.path.abspath(sys.argv[0]))
		IndexFiles(sys.argv[1],os.path.join(base_dir,INDEX_DIR),StandardAnalyzer(Version.LUCENE_CURRENT))
		end = datetime.now()
		print end-start
	except Exception,e:
		print "Failed:",e
		raise e

