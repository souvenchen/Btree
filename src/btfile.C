/*
 * btfile.C - function members of class BTreeFile 
 * 
 */

#include "minirel.h"
#include "buf.h"
#include "db.h"
#include "new_error.h"
#include "btfile.h"
#include "btreefilescan.h"
#include "btleaf_page.h"
#include "btindex_page.h"
#include "heapfile.h"
#include "sorted_page.h"
#include "bt.h"

// Define your error message here
const char* BtreeErrorMsgs[] = { "File not found",
};

static error_string_table btree_table( BTREE, BtreeErrorMsgs);

BTreeFile::BTreeFile (Status& returnStatus, const char *filename)
{
	PageId start_pg;
	pincount = 0;
	unpincount = 0;

	
	// filename should already exist. Else error.
	returnStatus = MINIBASE_DB->get_file_entry(filename, start_pg);

	returnStatus = MINIBASE_BM->pinPage(start_pg, (Page*&) bthead);

	this->filename = filename;
}

BTreeFile::BTreeFile (Status& returnStatus, const char *filename, 
                      const AttrType keytype,
                      const int keysize)
{
    PageId start_pg;
	pincount = 0;
	unpincount =0;

	// Read the header page and pin it.
    returnStatus = MINIBASE_DB->get_file_entry(filename, start_pg);

	// index file does not exist. create a new one.
	if ( OK != returnStatus )
	{
		++pincount;
		returnStatus = MINIBASE_BM->newPage(start_pg, (Page*&) bthead);
	
    	returnStatus = MINIBASE_DB->add_file_entry(filename, start_pg);
	}
	
	// Initialize the header page
    ((SortedPage *)bthead)->init(start_pg);
	((SortedPage *)bthead)->set_type(LEAF);

    // init the header page.
	bthead->root     = INVALID_PAGE;
    bthead->key_type = keytype;
    bthead->keysize  = keysize;
	this->filename = filename;
}

BTreeFile::~BTreeFile ()
{
	PageId header_page;
	// Unpin all the pages in the tree 
    MINIBASE_DB->get_file_entry(filename, header_page); 

    MINIBASE_BM->unpinPage(header_page,TRUE); 
		
}	

Status BTreeFile::destroyFile ()
{
	Status st;
	PageId pageno = bthead->root;
	Page *page;
	PageId temp;
	RID rid;
	Keytype key;
	PageId header_page;

    if ( OK != (st=MINIBASE_BM->pinPage(pageno, (Page*&) page)) )
         return MINIBASE_CHAIN_ERROR(BUFMGR, st);
	
	// Handle root page deletion here
	// 1. get the first pageno from root
	// 2. delete that subtree
	// 3. Once we're back, get the next pageno
	// 4. recurse
	// 5. When we run out of pagenos at root. free root.

	temp = ((BTIndexPage*)page)->getLeftLink();	

	while ( 1 )
	{		
		cout <<"";
		if ( temp == INVALID_PAGE )
			break;

		// delete this subtree
		if ( OK != destroyFilerecur(temp) )
			return MINIBASE_FIRST_ERROR(BTREE, DESTROY_FAILED);
	
		// get the next subtree
		// special case for first page
		if ( temp == ((BTIndexPage*)page)->getLeftLink() )
		{
			if ( OK != (st=((BTIndexPage*)page)->get_first(rid,&key,temp)) )
				return MINIBASE_CHAIN_ERROR(BTINDEXPAGE, st);
	
		}
		else
		{
	        if ( OK != (st=((BTIndexPage*)page)->get_next(rid,&key,temp)) )
            {
				// we have deleted the last entry from root page.
				if ( st == DONE )
					break;
				else
				    return MINIBASE_CHAIN_ERROR(BTINDEXPAGE, st);
			}	
		}

	}

	// We have deleted all the entires from root page.
	// Free the root and return OK.
	
    if ( OK != (st=MINIBASE_BM->unpinPage(pageno,TRUE)) )
         return MINIBASE_CHAIN_ERROR(BUFMGR, st);

	if ( OK != (st=MINIBASE_BM->freePage(pageno)) )	
		return MINIBASE_CHAIN_ERROR(BUFMGR, st);

	// free the header page too!
	if ( OK != MINIBASE_DB->get_file_entry(filename, header_page) )	
		return MINIBASE_CHAIN_ERROR(DBMGR, st);

	// free the header page too!
	if ( OK != MINIBASE_DB->get_file_entry(filename, header_page) )	
		return MINIBASE_CHAIN_ERROR(DBMGR, st);

    if ( OK != (st=MINIBASE_BM->unpinPage(header_page,TRUE)) )
         return MINIBASE_CHAIN_ERROR(BUFMGR, st);
	
    MINIBASE_BM->freePage(header_page);

    // Delete the file entry and return
    if ( OK != (st=MINIBASE_DB->delete_file_entry(filename)) )
        return MINIBASE_CHAIN_ERROR(DBMGR, st);
	
	return OK;
}
 

Status BTreeFile::destroyFilerecur(PageId pageno)
{
	Status st;
	Page *page;
	PageId temp;
	RID rid;
	Keytype key;
	
	if ( OK != (st=MINIBASE_BM->pinPage(pageno, (Page*&) page)) )	
		 return MINIBASE_CHAIN_ERROR(BUFMGR, st);

	if ( ((SortedPage*)page)->get_type() == INDEX )
	{
	    temp = ((BTIndexPage*)page)->getLeftLink();

			while ( 1 )
			{
				cout <<"";
				if ( temp == INVALID_PAGE )
					break;

				// delete this subtree
				if ( OK != destroyFilerecur(temp) )
					return MINIBASE_FIRST_ERROR(BTREE, DESTROY_FAILED);

				// get the next subtree
				// special case for first page
				if ( temp == ((BTIndexPage*)page)->getLeftLink() )
				{
					if ( OK != (st=((BTIndexPage*)page)->get_first(rid,&key,temp)) )
						return MINIBASE_CHAIN_ERROR(BTINDEXPAGE, st);

				}
				else
				{
					if ( OK != (st=((BTIndexPage*)page)->get_next(rid,&key,temp)) )
					{
						// we have deleted the last entry from root page.
						if ( st == DONE )
							break;
						else
                    		return MINIBASE_CHAIN_ERROR(BTINDEXPAGE, st);
					}
				}

			}

	    // We have deleted all the entires from root page.
    	// Free this page and return OK.
	    if ( OK != (st=MINIBASE_BM->unpinPage(pageno,TRUE)) )
         return MINIBASE_CHAIN_ERROR(BUFMGR, st);

	    if ( OK != (st=MINIBASE_BM->freePage(pageno)) )
    	    return MINIBASE_CHAIN_ERROR(BUFMGR, st);

    	return OK;
	}

	if ( ((SortedPage*)page)->get_type() == LEAF )
	{
	    if ( OK != (st=MINIBASE_BM->unpinPage(pageno)) )
	        return MINIBASE_CHAIN_ERROR(BUFMGR, st);

		 (st=MINIBASE_BM->freePage(pageno)); 
		//	return	MINIBASE_CHAIN_ERROR(BUFMGR, st);
 	}
	return OK;
}

Status BTreeFile::insert(const void *key, const RID rid) 
{
	Status st;
	PageId root_id = bthead->root;
	Page *page;
	void **splitkey; 
	PageId splitpgid = INVALID_PAGE;
    if (bthead->key_type == attrInteger)
        splitkey = (void **)new int;
    if (bthead->key_type == attrString)
        splitkey = (void **)new char[MAX_KEY_SIZE1];
	
	*splitkey = NULL;
	// root page is not alloc'ed yet
	if ( bthead->root == INVALID_PAGE )
	{
		++pincount;
		 if ( OK != (st=MINIBASE_BM->newPage(root_id, (Page*&)page)) )
			return MINIBASE_CHAIN_ERROR(BUFMGR, st);

	    // Initialize the root page
    	((SortedPage *)page)->init(root_id);
	    ((SortedPage *)page)->set_type(LEAF);

    	// init the root page.
	    bthead->root = root_id;

		++unpincount;
         if ( OK != (st=MINIBASE_BM->unpinPage(root_id, TRUE)) )
            return MINIBASE_CHAIN_ERROR(BUFMGR, st);
	}
	
	// recursively insert the pair. After the recursion comes back
    // check if we had a split and create a new node as necessary. 
	st = insertrecur(root_id,key,rid, splitkey, splitpgid);
	
	if ( *splitkey != NULL && splitpgid != INVALID_PAGE )
	{		
		if ( OK != (st = MINIBASE_BM->newPage(root_id, (Page*&)page)) )
			return MINIBASE_CHAIN_ERROR(BUFMGR, st);

        BTIndexPage *indexpage = (BTIndexPage*)page;
		indexpage->init(root_id);
        RID indexpage_rid;

		// set the left link to old header page
		indexpage->setLeftLink(bthead->root);

		// insert the splitkey and splitpgid
		if ( OK != (st=indexpage->insertKey(*splitkey,bthead->key_type,splitpgid,indexpage_rid)) )
			return MINIBASE_CHAIN_ERROR(BTINDEXPAGE, st);    	
		
        if ( OK != (st = MINIBASE_BM->unpinPage(root_id, TRUE)) )
            return MINIBASE_CHAIN_ERROR(BUFMGR, st);
	
		*splitkey  = NULL;
		splitpgid = INVALID_PAGE;
		bthead->root = root_id;
		return OK;
	}	

	if ( st == OK )
		return OK;

    return MINIBASE_FIRST_ERROR(BTREE, st);
	
}

Status BTreeFile::insertrecur(PageId pageno, const void *key, const RID rid, void **splitkey, PageId& splitpgid)
{
	Status st;
	Page *page;
	AttrType key_type = bthead->key_type;
	int len;

	// Pin the nodeid page
	if ( OK != (st = MINIBASE_BM->pinPage(pageno, (Page*&)page)) )
		return MINIBASE_CHAIN_ERROR(BUFMGR, st);

	// Deal with INDEX page
	if ( ((SortedPage *)page)->get_type() == INDEX )
	{
		BTIndexPage *indexpage = (BTIndexPage*)page;
		PageId tmppage;

		// Search for the page to insert this key-val pair
		if ( OK != (st = indexpage->get_page_no(key,key_type,tmppage)) )
            return MINIBASE_CHAIN_ERROR(BTINDEXPAGE, st);

        // Unpin this page before recursing.
		if ( OK != (st = MINIBASE_BM->unpinPage(pageno,TRUE)) )
           	return MINIBASE_CHAIN_ERROR(BUFMGR, st);

		// insert key-val pair into this page
        if ( OK != (st=this->insertrecur(tmppage,key,rid,splitkey,splitpgid)) )
            return MINIBASE_FIRST_ERROR(BTREE, st);

		// did not split child	
		if ( *splitkey == NULL && splitpgid == INVALID_PAGE)
		{
	      // if ( OK != (st = MINIBASE_BM->unpinPage(pageno,TRUE)) )
    	  //      return MINIBASE_CHAIN_ERROR(BUFMGR, st);
			return OK;
		}
	   
        // Pin the nodeid page
		if ( OK != (st = MINIBASE_BM->pinPage(pageno, (Page*&)page)) )
			return MINIBASE_CHAIN_ERROR(BUFMGR, st);

		indexpage = (BTIndexPage*)page;
		// We split the child entry.  
	    RID temprid;

        // insert the newchildentry
        st = indexpage->insertKey(*splitkey, bthead->key_type, splitpgid, temprid);

    	// found space for rec? we're done.
    	if ( st == OK )
        {
			*splitkey = NULL;
			splitpgid = INVALID_PAGE;
			if ( OK != (st = MINIBASE_BM->unpinPage(pageno,TRUE)) )
        	   	return MINIBASE_CHAIN_ERROR(BUFMGR, st);


			return OK;
		}

	    // NOSPACE ? split recursively.
    	if ( st != OK )
    	{
	        PageId newpgid;
    	    Page *newpage;

        	// first, alloc a new index page
        	if ( OK != (st = MINIBASE_BM->newPage(newpgid, (Page*&)newpage)) )
            	return MINIBASE_CHAIN_ERROR(BUFMGR, st);

			BTIndexPage *newindexpage = ((BTIndexPage*)newpage);
			newindexpage->init(newpgid);

            // Move first d key vals and d+1 pointers stay in present page - nodeid
            // Move last  d key vals and d+1 pointers to new page.
            // Count the rids present.

            // Count the rids present.
            PageId temp_pageid;
            RID temp_rid;
			RID newindex_rid;
			void *temp_key;
            if (key_type == attrInteger)
                temp_key = new int;
            if (key_type == attrString)
                temp_key = new char[MAX_KEY_SIZE1];

			// index interface should return first key-ptr. Leftmost will be copied using getLeftLink.
            if ( OK != (st = indexpage->get_first(temp_rid, temp_key, temp_pageid)) )
                return MINIBASE_CHAIN_ERROR(BTINDEXPAGE, st);

            // flag to indicate record was deleted
            int deleteflag = 0;
            int temp_count=1;
			int countrid = indexpage->numberOfRecords();
			PageId leftmost_pageid;

            // Go to d+1 entry.
            while ( temp_count < countrid )
            {
				if (deleteflag == 0 )
				{
                	if ( OK != (st = indexpage->get_next(temp_rid, temp_key, temp_pageid)) )
                    {
					    if ( st == DONE )
                            st = OK;
                        else
                            return MINIBASE_CHAIN_ERROR(BTLEAFPAGE, st);
					}
				}
                ++temp_count;
                if ( temp_count <= countrid/2 )
                    continue;
				
                // > d+1 entry. start moving this to new page.
                if ( OK != (st= newindexpage->insertKey(temp_key, key_type, temp_pageid, newindex_rid)) )
                    return MINIBASE_CHAIN_ERROR(BTINDEXPAGE, st);

                RID copy_indexpage_rid = temp_rid;
                // Before we delete entry, copy the next entry
                if ( OK != (st = indexpage->get_next(temp_rid, temp_key, temp_pageid)) )
                {
					if ( st == DONE )
						st = OK;
					else
					    return MINIBASE_CHAIN_ERROR(BTLEAFPAGE, st);
				}

                // delete the copied entry from old leaf page
                if ( OK != (st = indexpage->deleteKey(copy_indexpage_rid)) )
                    return MINIBASE_CHAIN_ERROR(BTLEAFPAGE, st);

                temp_rid = copy_indexpage_rid;
                deleteflag = 1;

			}
			// redistribution is complete. insert the *splitkey and splitpgid to the correct index page.
			// compare the first key from newindex page and insert the splitkey and splitpage into correct
			// indexpage.
            if ( OK != (st = newindexpage->get_first(temp_rid, temp_key, temp_pageid)) )
                return MINIBASE_CHAIN_ERROR(BTLEAFPAGE, st);
 
			if ( (keyCompare(*splitkey, temp_key, key_type) < 0) )
            {
                if ( OK != (st = indexpage->insertKey(*splitkey, key_type, splitpgid, temp_rid)) )
                    return MINIBASE_CHAIN_ERROR(BTLEAFPAGE, st);
            }
            else
            {
                if ( OK != (st = newindexpage->insertKey(*splitkey, key_type, splitpgid, newindex_rid)) )
                    return MINIBASE_CHAIN_ERROR(BTLEAFPAGE, st);
            }
		
			// Delete the first entry from new page and insert it into parent node.
	        if ( OK != (st = newindexpage->get_first(temp_rid, temp_key, temp_pageid)) )
                return MINIBASE_CHAIN_ERROR(BTLEAFPAGE, st);

			// set the left link to the record being deleted
			newindexpage->setLeftLink(temp_pageid);

            if ( OK != (st = newindexpage->deleteKey(temp_rid)) )
                return MINIBASE_CHAIN_ERROR(BTLEAFPAGE, st);

            *splitkey = temp_key;
			splitpgid = newpgid;
            ++unpincount;
            // Unpin this page before recursing.
            if ( OK != (st = MINIBASE_BM->unpinPage(pageno,TRUE)) )
                return MINIBASE_CHAIN_ERROR(BUFMGR, st);

            // Unpin this page before recursing.
            if ( OK != (st = MINIBASE_BM->unpinPage(newpgid,TRUE)) )
                return MINIBASE_CHAIN_ERROR(BUFMGR, st);

            return OK;

        }
	}

	// Leaf Page
	if ( ((SortedPage *)page)->get_type() == LEAF )
	{
        PageId newpgid;
        Page *newpage;
		RID entryrid;
		// these variables are only used when a leaf page is split
        void *insert_key;
        RID insert_rid;
        if (key_type == attrInteger)
           insert_key =new int;
        if (key_type == attrString)
            insert_key = new char[MAX_KEY_SIZE1];

		BTLeafPage *leafpage = (BTLeafPage*)page;

        // insert key-val pair into this page
        st=leafpage->insertRec(key,key_type,rid,entryrid);

		// found space 
		if ( OK == st )
        {
			++unpincount;
			*splitkey =  NULL;
			splitpgid = INVALID_PAGE;
        	// Unpin this page before recursing.
			if ( OK != (st = MINIBASE_BM->unpinPage(pageno, TRUE)) )
           		return MINIBASE_CHAIN_ERROR(BUFMGR, st);
        
           return OK;
		}

		// No space
		if ( st != OK )
		{
	        PageId newpgid;
    	    Page *newpage;
	
        	// first, alloc a new leaf page
        	if ( OK != (st = MINIBASE_BM->newPage(newpgid, (Page*&)newpage)) )
            	return MINIBASE_CHAIN_ERROR(BUFMGR, st);

			BTLeafPage *newleafpage = (BTLeafPage*)newpage;
			newleafpage->init(newpgid); // make the blank Page into a BTLeafPage 
		
        	// Move d+1 to 2d entries from present page to next.
	        // Count the rids present.
	        const int countrid = leafpage->numberOfRecords();
			int temp_count = 1;
        	RID temp_rid;
			RID leafpage_rid;
			RID newleafpage_rid;
			void *temp_key;
			if (key_type == attrInteger)
      			temp_key =new int;
  			if (key_type == attrString)
				temp_key = new char[MAX_KEY_SIZE1];
			
	        if ( OK != (st = leafpage->get_first(leafpage_rid, temp_key, temp_rid)) )
    	        return MINIBASE_CHAIN_ERROR(BTLEAFPAGE, st);

			// flag to indicate record was deleted
			int deleteflag = 0;
    	    // Go to d+1 entry.
	        while ( temp_count < countrid )
        	{
				if (deleteflag == 0 )
				{
	            	if ( OK != (st = leafpage->get_next(leafpage_rid, temp_key, temp_rid)) )
    	           	{
						if ( st == DONE )
							st = OK;
						else
						 	return MINIBASE_CHAIN_ERROR(BTLEAFPAGE, st);
					}
				}
		
	            ++temp_count;
				
    	        if ( temp_count <= countrid/2 )
        	        continue;
		
    	    	// >= d+1 entry. start moving this to new page.
        	    if ( OK != (st = newleafpage->insertRec(temp_key, key_type, temp_rid, newleafpage_rid)) )
            	    return MINIBASE_CHAIN_ERROR(BTLEAFPAGE, st);

			    RID	copy_leafpage_rid = leafpage_rid;
				// Before we delete entry, copy the next entry
                if ( OK != (st = leafpage->get_next(leafpage_rid, temp_key, temp_rid)) )
                {
                        if ( st == DONE )
                            st = OK;
                        else
                            return MINIBASE_CHAIN_ERROR(BTLEAFPAGE, st);
				}

				// delete the copied entry from old leaf page
				if ( OK != (st = leafpage->deleteKey(copy_leafpage_rid)) )
					return MINIBASE_CHAIN_ERROR(BTLEAFPAGE, st); 
		
				leafpage_rid = copy_leafpage_rid;
				deleteflag = 1;
			}

            // compare the first key from newpage with given key.
            if ( OK != (st = newleafpage->get_first(newleafpage_rid, temp_key, temp_rid)) )
                return MINIBASE_CHAIN_ERROR(BTLEAFPAGE, st);

			if (  (keyCompare(key, temp_key, key_type) < 0) )
	        {
			    if ( OK != (st = leafpage->insertRec(key, key_type, rid, leafpage_rid)) )
    	            return MINIBASE_CHAIN_ERROR(BTLEAFPAGE, st);
			}
			else
			{
              	if ( OK != (st = newleafpage->insertRec(key, key_type, rid, newleafpage_rid)) )
                  	return MINIBASE_CHAIN_ERROR(BTLEAFPAGE, st);
			}
			
			// set sibling pointers in leaf page.
			PageId saveoldprev = leafpage->getPrevPage();
			PageId saveoldnext = leafpage->getNextPage();
				
			leafpage->setNextPage(newpgid);
			newleafpage->setPrevPage(pageno);	
			newleafpage->setNextPage(saveoldnext);

			// for now.
            if ( OK != (st = newleafpage->get_first(newleafpage_rid, temp_key, temp_rid)) )
                return MINIBASE_CHAIN_ERROR(BTLEAFPAGE, st);
	
			*splitkey = temp_key;
			splitpgid = newpgid;

			++unpincount;
			// Unpin this page before recursing.
			if ( OK != (st = MINIBASE_BM->unpinPage(pageno,TRUE)) )
           		return MINIBASE_CHAIN_ERROR(BUFMGR, st);

            // Unpin this page before recursing.
            if ( OK != (st = MINIBASE_BM->unpinPage(newpgid,TRUE)) )
                return MINIBASE_CHAIN_ERROR(BUFMGR, st);

			return OK;
		}
	}
	return MINIBASE_FIRST_ERROR(BTREE, st);
}

Status BTreeFile::Delete(const void *key, const RID rid)
{
	Status st;
	PageId pageno = bthead->root;

	if ( OK != (st=this->Deleterecur(pageno,key,rid)) )
		return MINIBASE_FIRST_ERROR(BTINDEXPAGE, BTREE_DELETE_FAILED);

	return OK;
}
    
Status BTreeFile::Deleterecur(PageId pageno, const void *key, const RID rid)
{
	Status st;
	Page *page;
	RID leafpage_rid;
	Keytype temp_key;

	if ( OK != (st=MINIBASE_BM->pinPage(pageno, (Page*&) page)) )
		return MINIBASE_CHAIN_ERROR(BUFMGR, st);

    if ( ((SortedPage *)page)->get_type() == INDEX )
    {
        BTIndexPage *index = (BTIndexPage *)page;
		PageId searchpage;

        // search the key
        if ( OK != (st=index->get_page_no(key,bthead->key_type,searchpage)) )
            return MINIBASE_CHAIN_ERROR(BTINDEXPAGE, st);

        // unpin before we recurse
        if ( OK != (st=MINIBASE_BM->unpinPage(pageno,TRUE)) )
            return MINIBASE_CHAIN_ERROR(BUFMGR, st);

        if ( OK != (st=this->Deleterecur(searchpage,key,rid)) )
            return MINIBASE_FIRST_ERROR(BTINDEXPAGE, BTREE_DELETE_FAILED);
		
		return OK;
	}

    if ( ((SortedPage *)page)->get_type() == LEAF )
    {
 		BTLeafPage *leafpage = ((BTLeafPage*)page);
		RID leafpage_rid;
		RID data_rid;

		// Look for this key.rid pair
		if ( OK != (st = leafpage->get_first(leafpage_rid, &temp_key, data_rid)) )
            return MINIBASE_CHAIN_ERROR(BTLEAFPAGE, st);
	
        while( keyCompare(&temp_key,key, bthead->key_type) < 0 )
        {
            cout << "";
            if ( OK != (st=leafpage->get_next(leafpage_rid,&temp_key,data_rid)) )
            {
                // get next leaf level page before unpinning.
                PageId temp = ((HFPage*)page)->getNextPage();

                //rid not found in the first leaf. continue searching.
                if ( OK != (st=MINIBASE_BM->unpinPage(pageno,TRUE)) )
                    return MINIBASE_CHAIN_ERROR(BTLEAFPAGE, st); 

                if ( temp == INVALID_PAGE )
                {
                    return DONE;                    
                }

                pageno = temp;

                if ( OK != (st=MINIBASE_BM->pinPage(pageno, (Page*&) page)) )
                    return MINIBASE_CHAIN_ERROR(BTLEAFPAGE, st);

                leafpage = (BTLeafPage *)page;
                if ( OK != (st=leafpage->get_first(leafpage_rid,&temp_key,data_rid)) )
                    return MINIBASE_CHAIN_ERROR(BTLEAFPAGE, st);
            }
        }

		// we found the data entry. Delete and return.	
		if ( OK != (st = leafpage->deleteKey(leafpage_rid)) )
			return MINIBASE_CHAIN_ERROR(BTLEAFPAGE, st);

        if ( OK != (st=MINIBASE_BM->unpinPage(pageno, TRUE)) )
            return MINIBASE_CHAIN_ERROR(BUFMGR, st);
	
		return OK;
	
	}
	return MINIBASE_FIRST_ERROR(BTINDEXPAGE, BTREE_DELETE_FAILED);

}

IndexFileScan *BTreeFile::new_scan(const void *lo_key, const void *hi_key) 
{
	Status st;
	Page *page;
	PageId pageno = bthead->root;
	BTreeFileScan *scanner = new BTreeFileScan();
	Keytype key;
	AttrType key_type = bthead->key_type;	
	
	if ( bthead->root == INVALID_PAGE )
		return scanner;

	// Case 1: scan all the data entries
	// Pin the root page
	if ( lo_key == NULL && hi_key == NULL )
	{
		RID rid;
		
		++pincount;
		// return the first leaf page
		if ( OK != (st=MINIBASE_BM->pinPage(bthead->root, (Page*&) page)) )
 			return scanner;
		
		pageno = bthead->root;
		if ( ((SortedPage *)page)->get_type() == INDEX )
		{
			PageId temp;

			while ( ((SortedPage *)page)->get_type() == INDEX )
			{
				cout <<"";
				BTIndexPage *index = (BTIndexPage*)page;

				if ( INVALID_PAGE == (temp = index->getLeftLink()) )
					return scanner;
		    
			    if ( OK != (st=MINIBASE_BM->unpinPage(pageno,TRUE)) )
    		        return scanner;
				
				pageno = temp;
				if ( OK != (st=MINIBASE_BM->pinPage(pageno, (Page*&) page)) )
					return scanner;
			}
		}
		
		RID datarid;
		// we are at the first leaf page
		if ( ((SortedPage *)page)->get_type() != LEAF )
			return scanner;

		BTLeafPage *leaf = (BTLeafPage *)page;
		// Point to the first record 
		if ( OK != (st=leaf->get_first(rid,&key,datarid)) )
			return scanner;							

		scanner->currentRid = rid;
		scanner->nextrid	= rid;
		scanner->currentpage = pageno;
		scanner->lo_key = lo_key;
		scanner->hi_key = hi_key;
		scanner->key_size = bthead->keysize;
		scanner->ktype = bthead->key_type;
		scanner->valid = 1;

        if ( OK != (st=MINIBASE_BM->unpinPage(pageno,TRUE)) )
            return scanner;

		return scanner;
	}

	// Case 2: range scan from min to the hi_key
	if ( (lo_key == NULL) && (hi_key != NULL) )
	{
		RID rid;
		
		// return the first leaf page
		if ( OK != (st=MINIBASE_BM->pinPage(bthead->root, (Page*&) page)) )
 			return scanner;

		pageno = bthead->root;

        if ( ((SortedPage *)page)->get_type() == INDEX )
        {
            PageId temp;

            while ( ((SortedPage *)page)->get_type() == INDEX )
            {
				cout<<"";
                BTIndexPage *index = (BTIndexPage*)page;

                if ( INVALID_PAGE == (temp = index->getLeftLink()) )
                    return scanner;

                if ( OK != (st=MINIBASE_BM->unpinPage(pageno,TRUE)) )
                    return scanner;

                pageno = temp;
                if ( OK != (st=MINIBASE_BM->pinPage(pageno, (Page*&) page)) )
                    return scanner;
            }
        }

		RID datarid;
		// we are at the first leaf page
		if ( ((SortedPage *)page)->get_type() != LEAF )
			return scanner;

		BTLeafPage *leaf = (BTLeafPage *)page;
		// Point to the first record 
		if ( OK != (st=leaf->get_first(rid,&key,datarid)) )
			return scanner;							

		scanner->currentRid = rid;
		scanner->nextrid    = rid;
		scanner->currentpage = pageno;
		scanner->lo_key = lo_key;
		scanner->hi_key = hi_key;
		scanner->key_size = bthead->keysize;
		scanner->ktype = bthead->key_type;
		scanner->valid = 1;

        if ( OK != (st=MINIBASE_BM->unpinPage(pageno,TRUE)) )
            return scanner;

		return scanner;
	}

	// Case 3: range scan from the lo_key to max
	if ( lo_key != NULL && hi_key == NULL )
	{
        RID rid;

        // return the first leaf page
        if ( OK != (st=MINIBASE_BM->pinPage(bthead->root, (Page*&) page)) )
            return scanner;

        pageno = bthead->root;

        if ( ((SortedPage *)page)->get_type() == INDEX )
        {
            PageId temp;

            while ( ((SortedPage *)page)->get_type() == INDEX )
            {
				cout <<"";
                if ( INVALID_PAGE == (temp = ((BTIndexPage*)page)->getLeftLink()) )
                    return scanner;

                if ( OK != (st=MINIBASE_BM->unpinPage(pageno,TRUE)) )
                    return scanner;

                pageno = temp;
                if ( OK != (st=MINIBASE_BM->pinPage(pageno, (Page*&) page)) )
                    return scanner;
            }
        }
		

        RID datarid;

        if ( ((SortedPage *)page)->get_type() != LEAF )
            return scanner;

        BTLeafPage *leaf = (BTLeafPage *)page;
		
        if ( OK != (st=leaf->get_first(rid,&key,datarid)) )
            return scanner;
			
		while( keyCompare(&key,lo_key,key_type) < 0 )
		{
			cout << "";
			if ( OK != (st=leaf->get_next(rid,&key,datarid)) )
			{
				// get next leaf level page before unpinning.
				PageId temp = ((HFPage*)page)->getNextPage();

				//rid not found in the first leaf. continue searching.
                if ( OK != (st=MINIBASE_BM->unpinPage(pageno,TRUE)) )
                    return scanner;
			
				if ( temp == INVALID_PAGE )
				{
					scanner->currentpage = INVALID_PAGE;
					return scanner;
				}

				pageno = temp;

				if ( OK != (st=MINIBASE_BM->pinPage(pageno, (Page*&) page)) )
                    return scanner;
				
				leaf = (BTLeafPage *)page;
		        if ( OK != (st=leaf->get_first(rid,&key,datarid)) )
    		        return scanner;
			}
		}

        scanner->currentRid = rid;
		scanner->nextrid    = rid;
		scanner->currentpage = pageno;
        scanner->lo_key = lo_key;
        scanner->hi_key = hi_key;
		scanner->key_size = bthead->keysize;
		scanner->ktype = bthead->key_type;
		scanner->valid = 1;
        if ( OK != (st=MINIBASE_BM->unpinPage(pageno,TRUE)) )
            return scanner;
        return scanner;
		
	}

	// Case 4: exact match ( might not unique)
	if ( (lo_key != NULL) && (hi_key != NULL) && (keyCompare(lo_key,hi_key,key_type) == 0) )
	{
        RID rid;
        Page *page;

		++pincount;
        // return the first leaf page
        if ( OK != (st=MINIBASE_BM->pinPage(bthead->root, (Page*&) page)) )
            return scanner;

        pageno = bthead->root;

        if ( ((SortedPage *)page)->get_type() == INDEX )
        {
            PageId temp;

            while ( ((SortedPage *)page)->get_type() == INDEX )
            {
                cout <<"";
                if ( INVALID_PAGE == (temp = ((BTIndexPage*)page)->getLeftLink()) )
                    return scanner;

                if ( OK != (st=MINIBASE_BM->unpinPage(pageno,TRUE)) )
                    return scanner;

                pageno = temp;
                if ( OK != (st=MINIBASE_BM->pinPage(pageno, (Page*&) page)) )
                    return scanner;
            }
        }

        RID datarid;

        if ( ((SortedPage *)page)->get_type() != LEAF )
            return scanner;

        BTLeafPage *leaf = (BTLeafPage *)page;

        if ( OK != (st=leaf->get_first(rid,&key,datarid)) )
            return scanner;

        while( keyCompare(&key,lo_key,key_type) < 0 )
        {
			cout << "";
            if ( OK != (st=leaf->get_next(rid,&key,datarid)) )
            {
                // get next leaf level page before unpinning.
                PageId temp = ((HFPage*)page)->getNextPage();

                //rid not found in the first leaf. continue searching.
                if ( OK != (st=MINIBASE_BM->unpinPage(pageno,TRUE)) )
                    return scanner;

                if ( temp == INVALID_PAGE )
                {
                    scanner->currentpage = INVALID_PAGE;
                    return scanner;
                }

                pageno = temp;

                if ( OK != (st=MINIBASE_BM->pinPage(pageno, (Page*&) page)) )
                    return scanner;

                leaf = (BTLeafPage *)page;
                if ( OK != (st=leaf->get_first(rid,&key,datarid)) )
                    return scanner;
            }
        }

        scanner->currentRid = rid;
		scanner->nextrid    = rid;
        scanner->currentpage = pageno;
		scanner->lo_key = lo_key;
		scanner->hi_key = hi_key;
		scanner->key_size = bthead->keysize;
		scanner->ktype = bthead->key_type;
		scanner->valid = 1;
        if ( OK != (st=MINIBASE_BM->unpinPage(pageno,TRUE)) )
            return scanner;
	
		return scanner;
	}

    // Case 5: range scan from lo_key to hi_key
    if ( (lo_key != NULL) && (hi_key != NULL) && (keyCompare(lo_key,hi_key,key_type) < 0) )
    {

        RID rid;
        Page *page;

        // return the first leaf page
        if ( OK != (st=MINIBASE_BM->pinPage(bthead->root, (Page*&) page)) )
            return scanner;

        pageno = bthead->root;

        if ( ((SortedPage *)page)->get_type() == INDEX )
        {
            PageId temp;

            while ( ((SortedPage *)page)->get_type() == INDEX )
            {
                cout <<"";
                if ( INVALID_PAGE == (temp = ((BTIndexPage*)page)->getLeftLink()) )
                    return scanner;

                if ( OK != (st=MINIBASE_BM->unpinPage(pageno,TRUE)) )
                    return scanner;

                pageno = temp;
                if ( OK != (st=MINIBASE_BM->pinPage(pageno, (Page*&) page)) )
                    return scanner;
            }
        }

        RID datarid;

        if ( ((SortedPage *)page)->get_type() != LEAF )
            return scanner;

        BTLeafPage *leaf = (BTLeafPage *)page;

        if ( OK != (st=leaf->get_first(rid,&key,datarid)) )
            return scanner;

        while( keyCompare(&key,lo_key,key_type) < 0 )
        {
			cout << "";
            if ( OK != (st=leaf->get_next(rid,&key,datarid)) )
            {
                // get next leaf level page before unpinning.
                PageId temp = ((HFPage*)page)->getNextPage();

                //rid not found in the first leaf. continue searching.
                if ( OK != (st=MINIBASE_BM->unpinPage(pageno,TRUE)) )
                    return scanner;
 
                if ( temp == INVALID_PAGE )
                {
                    scanner->currentpage = INVALID_PAGE;
                    return scanner;
                }

                pageno = temp;

                if ( OK != (st=MINIBASE_BM->pinPage(pageno, (Page*&) page)) )
                    return scanner;

                leaf = (BTLeafPage *)page;
                if ( OK != (st=leaf->get_first(rid,&key,datarid)) )
                    return scanner;
            }
        }

        scanner->currentRid = rid;
		scanner->nextrid    = rid;
        scanner->currentpage = pageno;
		scanner->lo_key = lo_key;
		scanner->hi_key = hi_key;
		scanner->key_size = bthead->keysize;
		scanner->ktype = bthead->key_type;
		scanner->valid = 1;
        if ( OK != (st=MINIBASE_BM->unpinPage(pageno,TRUE)) )
            return scanner;
		return scanner;
	}
	
	// lo_key > hi_key
	if ( keyCompare(lo_key,hi_key,key_type) > 0 )
		scanner->valid = 0;
		
	return scanner;		
}


