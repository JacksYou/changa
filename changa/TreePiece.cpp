/** @file TreePiece.cpp
 */

#include <cstdio>
#include <algorithm>
#include <fstream>
#include <assert.h>

//#include "ComlibManager.h"

#include "ParallelGravity.h"
#include "CacheManager.h"

#include "Space.h"
#include "gravity.h"

using namespace std;
using namespace SFC;
using namespace TreeStuff;
using namespace TypeHandling;

int TreeStuff::maxBucketSize;

void TreePiece::load(const std::string& fn, const CkCallback& cb) {
  basefilename = fn;
	
  //read in particles
  XDR xdrs;
  FILE* infile = fopen((basefilename + ".mass").c_str(), "rb");
  if(!infile) {
    cerr << "TreePiece " << thisIndex << ": Couldn't open masses file, aborting" << endl;
    CkAbort("Badness");
  }
  xdrstdio_create(&xdrs, infile, XDR_DECODE);
	
  if(!xdr_template(&xdrs, &fh)) {
    cerr << "TreePiece " << thisIndex << ": Couldn't read header from masses file, aborting" << endl;
    CkAbort("Badness");
  }
	
  if(fh.magic != FieldHeader::MagicNumber || fh.dimensions != 1 || fh.code != float32) {
    cerr << "TreePiece " << thisIndex << ": Masses file is corrupt or of incorrect type, aborting" << endl;
    CkAbort("Badness");
  }
		
  myNumParticles = fh.numParticles / numTreePieces;
  if(verbosity >= 1)
    if(thisIndex == 0)
      cerr << "Total num of particles: " << fh.numParticles << endl;
	
  unsigned int excess = fh.numParticles % numTreePieces;
  unsigned int startParticle = myNumParticles * thisIndex;
  if(thisIndex < (int) excess) {
    myNumParticles++;
    startParticle += thisIndex;
  } else
    startParticle += excess;
	
  if(verbosity > 3)
    cerr << "TreePiece " << thisIndex << ": Of " << fh.numParticles << " particles, taking " << startParticle << " through " << (startParticle + myNumParticles - 1) << endl;

  myParticles = new GravityParticle[myNumParticles + 2];
	
  float mass;
  float maxMass;
  if(!xdr_template(&xdrs, &mass) || !xdr_template(&xdrs, &maxMass)) {
    cerr << "TreePiece " << thisIndex << ": Problem reading beginning of the mass file, aborting" << endl;
    CkAbort("Badness");
  }
	
  if(mass == maxMass) { //all the same mass
    for(u_int64_t i = 0; i < myNumParticles; ++i){
      myParticles[i + 1].mass = mass;
			/********************************/
			myParticles[i + 1].intcellmass = 0;
			myParticles[i + 1].intpartmass = 0;
			myParticles[i + 1].extcellmass = 0;
			myParticles[i + 1].extpartmass = 0;
			/********************************/
		}
		/**************************/	
  	piecemass = myNumParticles*mass;
		//cerr << "In a tree piece....mass of tree piece particles: " << piecemass << ", single particle; " << mass;
		/*************************/
	} else {

    if(!seekField(fh, &xdrs, startParticle)) {
      cerr << "TreePiece " << thisIndex << ": Could not seek to my part of the mass file, aborting" << endl;
      CkAbort("Badness");
    }

    for(u_int64_t i = 0; i < myNumParticles; ++i) {
      if(!xdr_template(&xdrs, &mass)) {
				cerr << "TreePiece " << thisIndex << ": Problem reading my part of the mass file, aborting" << endl;
				CkAbort("Badness");
      }
      myParticles[i + 1].mass = mass;
			/****************************/
			myParticles[i + 1].intcellmass = 0;
			myParticles[i + 1].intpartmass = 0;
			myParticles[i + 1].extcellmass = 0;
			myParticles[i + 1].extpartmass = 0;
			piecemass += mass;
			/****************************/
    }
  }
	
  xdr_destroy(&xdrs);
  fclose(infile);
	
  for(u_int64_t i = 0; i < myNumParticles; ++i)
    myParticles[i + 1].soft = 0.0;

  infile = fopen((basefilename + ".pos").c_str(), "rb");
  if(!infile) {
    cerr << "TreePiece " << thisIndex << ": Couldn't open positions file, aborting" << endl;
    CkAbort("Badness");
  }
  xdrstdio_create(&xdrs, infile, XDR_DECODE);
	
  FieldHeader posHeader;
  if(!xdr_template(&xdrs, &posHeader)) {
    cerr << "TreePiece " << thisIndex << ": Couldn't read header from positions file, aborting" << endl;
    CkAbort("Badness");
  }
	
  if(posHeader.magic != FieldHeader::MagicNumber || posHeader.dimensions != 3 || posHeader.code != float32) {
    cerr << "TreePiece " << thisIndex << ": Positions file is corrupt or of incorrect type, aborting" << endl;
    CkAbort("Badness");
  }
	
  if(posHeader.time != fh.time || posHeader.numParticles != fh.numParticles) {
    cerr << "TreePiece " << thisIndex << ": Positions file doesn't match masses file, aborting" << endl;
    CkAbort("Badness");
  }
	
  Vector3D<float> pos;
  Vector3D<float> maxPos;
  if(!xdr_template(&xdrs, &pos) || !xdr_template(&xdrs, &maxPos)) {
    cerr << "TreePiece " << thisIndex << ": Problem reading beginning of the positions file, aborting" << endl;
    CkAbort("Badness");
  }
	
  boundingBox.lesser_corner = pos;
  boundingBox.greater_corner = maxPos;
	
  if(pos == maxPos) { //all the same position
    //XXX This would be bad!
    Key k = generateKey(pos, boundingBox);
    for(u_int64_t i = 0; i < myNumParticles; ++i) {
      myParticles[i + 1].position = pos;
      myParticles[i + 1].key = k;
    }
  } else {
    if(!seekField(posHeader, &xdrs, startParticle)) {
      cerr << "TreePiece " << thisIndex << ": Could not seek to my part of the positions file, aborting" << endl;
      CkAbort("Badness");
    }

    Key previous = 0;  // hold the last key generated
    Key current;
    //read all my particles' positions and make keys
    for(u_int64_t i = 0; i < myNumParticles; ++i) {
      if(!xdr_template(&xdrs, &pos)) {
	cerr << "TreePiece " << thisIndex << ": Problem reading my part of the positions file, aborting" << endl;
	CkAbort("Badness");
      }
      myParticles[i + 1].position = pos;
      current = generateKey(pos, boundingBox);
      myParticles[i + 1].key = current;
      if (current < previous) {
	CkPrintf("TreePiece %d: Key not ordered! (%016llx)\n",thisIndex,current);
      }
      previous = current;
    }
  }
	
  xdr_destroy(&xdrs);
  fclose(infile);
	
  if(verbosity > 3)
    cerr << "TreePiece " << thisIndex << ": Read in masses and positions" << endl;
	
  contribute(0, 0, CkReduction::concat, cb);
}

void TreePiece::buildTree(int bucketSize, const CkCallback& cb) {
  maxBucketSize = bucketSize;
  callback = cb;
  Key bounds[2];
  sort(myParticles+1, myParticles+myNumParticles+1);
  bounds[0] = myParticles[1].key;
  bounds[1] = myParticles[myNumParticles].key;
  contribute(2 * sizeof(Key), bounds, CkReduction::concat, CkCallback(CkIndex_TreePiece::collectSplitters(0), thisArrayID));
}

class KeyDouble {
  Key first;
  Key second;
public:
  inline bool operator<(const KeyDouble& k) const {
    return first < k.first;
  }
};

void TreePiece::collectSplitters(CkReductionMsg* m) {
  numSplitters = 2 * numTreePieces;
  splitters = new Key[numSplitters];
  Key* splits = static_cast<Key *>(m->getData());
  copy(splits, splits + numSplitters, splitters);
  KeyDouble* splitters2 = (KeyDouble *)splitters;
  //sort(splitters, splitters + numSplitters);
  sort(splitters2, splitters2 + numTreePieces);
  for (int i=1; i<numSplitters; ++i) {
    if (splitters[i] < splitters[i-1]) {
      //for (int j=0; j<numSplitters; ++j) CkPrintf("%d: Key %d = %016llx\n",thisIndex,j,splitters[j]);
      CkAbort("Keys not ordered");
    }
  }
  contribute(0, 0, CkReduction::concat, CkCallback(CkIndex_TreePiece::startTreeBuild(0), thisArrayID));
  delete m;
  if(verbosity > 3)
    cerr << "TreePiece " << thisIndex << ": Collected splitters" << endl;
}

void TreePiece::startTreeBuild(CkReductionMsg* m) {
  delete m;
	
  if(thisIndex == 0)
    myParticles[0].key = firstPossibleKey;
  else
    myParticles[0].key = splitters[2 * thisIndex - 1];
	
  if(thisIndex == (int) numTreePieces - 1)
    myParticles[myNumParticles + 1].key = lastPossibleKey;
  else
    myParticles[myNumParticles + 1].key = splitters[2 * thisIndex + 2];
	
  leftBoundary = myParticles;
  rightBoundary = myParticles + myNumParticles + 1;

  //cerr << "Piece " << (myParticles + 1)->key << " : " << (myParticles + myNumParticles)->key << " has leftBoundary: " << leftBoundary->key << " rightBoundary: " << rightBoundary->key << endl;
	
  root = new SFCTreeNode;
  root->key = firstPossibleKey;
  root->boundingBox = boundingBox;
  nodeLookup[root->lookupKey()] = root;
  numBuckets = 0;
  bucketList.clear();
	
  boundaryNodesPending = 0;
	
  if(verbosity > 3)
    cerr << "TreePiece " << thisIndex << ": Starting tree build" << endl;
	
  buildTree(root, leftBoundary, rightBoundary);
	
  if(boundaryNodesPending == 0)
    contribute(0, 0, CkReduction::concat, callback);
	
  if(verbosity > 3)
    cerr << "TreePiece " << thisIndex << ": Number of buckets: " << numBuckets << endl;
  if(verbosity > 3)
    cerr << "TreePiece " << thisIndex << ": Finished tree build, resolving boundary nodes" << endl;
}

/// Find what chare this node's left child resides on, and create it
inline SFCTreeNode* TreePiece::lookupLeftChild(SFCTreeNode* node) {
  SFCTreeNode* child = node->createLeftChild();
  nodeLookup[child->lookupKey()] = child;
  child->setType(NonLocal);
  tempNode.key = node->key;
  tempNode.level = node->level + 1;
  if(!nodeOwnership(&tempNode, &child->remoteIndex, &child->numOwners)){
    cerr << "This is surprising, but may get taken care of." << endl;
    node->leftChild=NULL;
    nodeLookup.erase(child->lookupKey());
    delete child;
    return NULL;
  }	
  return child;
}

inline SFCTreeNode* TreePiece::lookupRightChild(SFCTreeNode* node) {
  SFCTreeNode* child = node->createRightChild();
  nodeLookup[child->lookupKey()] = child;
  child->setType(NonLocal);
  tempNode.key = node->rightChildKey();
  tempNode.level = node->level + 1;
  if(!nodeOwnership(&tempNode, &child->remoteIndex, &child->numOwners)){
    cerr << "This is surprising, but may get taken care of." << endl;
    node->rightChild=NULL;
    nodeLookup.erase(child->lookupKey());
    delete child;
    return NULL;
  }	
  return child;
}

/// Determine if the node is owned, by how many and whom, and designate a "head" owner
inline bool TreePiece::nodeOwnership(SFCTreeNode* node, unsigned int* designatedOwner, unsigned int* numOwners, unsigned int* firstOwner, unsigned int* lastOwner) {
  //find the first place in the array splitters the left boundary of the node can go
  Key* locLeft = upper_bound(splitters, splitters + numSplitters, node->leftBoundary());
  //find the last place the right boundary can go
  Key* locRight = lower_bound(locLeft, splitters + numSplitters, node->rightBoundary());
  if(locLeft == locRight) { //node fits between two splitters
    if((locLeft - splitters) % 2) { //it fits inside a TreePiece
      if(numOwners)
	*numOwners = 1;
      unsigned int owner = (locLeft - splitters) / 2;
      if(firstOwner)
	*firstOwner = owner;
      if(lastOwner)
	*lastOwner = owner;
      if(designatedOwner)
	*designatedOwner = owner;
      return true;
    } else { //it falls between TreePieces
      cerr << "Wow, I didn't think this could happen.  Live and learn." << endl;
      return false;
    }
  } else {
    //the index of first co-owner of the node
    unsigned int first = (locLeft - splitters) / 2;
    //the index of the last co-owner of the node
    unsigned int last = (locRight - splitters - 1) / 2;
    if(numOwners)
      *numOwners = last - first + 1;
    if(firstOwner)
      *firstOwner = first;
    if(lastOwner)
      *lastOwner = last;
    if(designatedOwner) //the designated owner is the one in the middle
      *designatedOwner = (first + last) / 2;
    return true;
  }
}

/** A recursive algorithm for building my tree.
    Examines successive bits in the particles' keys, looking for splits.
    Each bit is a level of nodes in the tree.  We keep going down until
    we can bucket the particles.  The left and right boundaries of this
    piece of tree will point to other pieces on other chares in the array.
*/
void TreePiece::buildTree(SFCTreeNode* node, GravityParticle* leftParticle, GravityParticle* rightParticle) {
  node->beginParticle = leftParticle - myParticles;
  node->endParticle = (rightParticle - myParticles) + 1;
  if(leftParticle == leftBoundary)
    node->beginParticle++;
  if(rightParticle == rightBoundary)
    node->endParticle--;
	
  //check if we should bucket these particles
  if(rightParticle - leftParticle < maxBucketSize) {
    //can't bucket until we've cut at the boundary
    if((leftParticle != leftBoundary) && (rightParticle != rightBoundary)) {
      node->setType(Bucket);
      node->numOwners = 1;
      for(GravityParticle* iter = leftParticle; iter != rightParticle + 1; ++iter)
	node->moments += *iter;
      // calculateRadiusFarthestCorner(node->moments, node->boundingBox);
      calculateRadiusFarthestParticle(node->moments, leftParticle, rightParticle + 1);
      bucketList.push_back(node);
      numBuckets++;
      return;
    }
  } else if(node->level == 63) {
    cerr << thisIndex << ": TreePiece: This piece of tree has exhausted all the bits in the keys.  Super double-plus ungood!" << endl;
    cerr << "Left particle: " << (leftParticle - myParticles) << " Right particle: " << (rightParticle - myParticles) << endl;
    cerr << "Left key : " << keyBits(leftParticle->key, 63) << endl;
    cerr << "Right key: " << keyBits(rightParticle->key, 63) << endl;
    return;
  }
	
  //this is the bit we are looking at
  Key currentBitMask = static_cast<Key>(1) << (62 - node->level);
  //we need to know the bit values at the left and right
  Key leftBit = leftParticle->key & currentBitMask;
  Key rightBit = rightParticle->key & currentBitMask;
  SFCTreeNode* child;
	
  if(leftBit < rightBit) { //a split at this level
    //find the split by looking for where the key with the bit switched on could go
    GravityParticle* splitParticle = lower_bound(leftParticle, rightParticle + 1, node->key | currentBitMask);
    if(splitParticle == leftBoundary + 1) {
      //we need to make the left child point to a remote chare
      if(thisIndex != 0) //the left-most chare can't point any further left
	lookupLeftChild(node);
      child = node->createRightChild();
      nodeLookup[child->lookupKey()] = child;
      buildTree(child, splitParticle, rightParticle);
    } else if(splitParticle == rightBoundary) {
      //we need to make the right child point to a remote chare
      child = node->createLeftChild();
      nodeLookup[child->lookupKey()] = child;
      buildTree(child, leftParticle, splitParticle - 1);
      if(thisIndex != (int) numTreePieces - 1) //the right-most chare can't point any further right
	lookupRightChild(node);
    } else {
      //neither child is remote, keep going with them
      child = node->createLeftChild();
      nodeLookup[child->lookupKey()] = child;
      buildTree(child, leftParticle, splitParticle - 1);
      child = node->createRightChild();
      nodeLookup[child->lookupKey()] = child;
      buildTree(child, splitParticle, rightParticle);
    }
  } else if(leftBit & rightBit) { //both ones, make a right child
    //should the left child be remote?
    if(leftParticle == leftBoundary && thisIndex != 0)
      lookupLeftChild(node);
    child = node->createRightChild();
    nodeLookup[child->lookupKey()] = child;
    buildTree(child, leftParticle, rightParticle);
  } else if(leftBit > rightBit) {
    cerr << "Bits not right: " << leftBit << " vs " << rightBit << endl;
    cerr << "Left particle: " << (leftParticle - myParticles) << " Right particle: " << (rightParticle - myParticles) << endl;
    cerr << "Left key : " << keyBits(leftParticle->key, 63) << endl;
    cerr << "Right key: " << keyBits(rightParticle->key, 63) << endl;
    return;
  } else { //both zeros, make a left child
    child = node->createLeftChild();
    nodeLookup[child->lookupKey()] = child;
    buildTree(child, leftParticle, rightParticle);
    //should the right child be remote?
    if(rightParticle == rightBoundary && thisIndex != (int) numTreePieces - 1)
      lookupRightChild(node);
  }
	
  //children have been formed, do bottom-up collection
  if(node->leftChild) {
    node->moments += dynamic_cast<SFCTreeNode *>(node->leftChild)->moments;
  }
  if(node->rightChild) {
    node->moments += dynamic_cast<SFCTreeNode *>(node->rightChild)->moments;
  }
	
  // figure out if this is a boundary node
  if((leftParticle == leftBoundary && thisIndex != 0) || (rightParticle == rightBoundary && thisIndex != (int) numTreePieces - 1)) {
    //send my information about this node to the designated owner
    unsigned int designatedOwner;
    nodeOwnership(node, &designatedOwner, &node->numOwners);
    boundaryNodesPending++;
    //in boundary nodes, remoteIndex contains the total number of particles in the node, from all the co-owners
    //endParticle - beginParticle tells you how many particles are actually on this processor
    node->remoteIndex = node->endParticle - node->beginParticle;
    if((int) designatedOwner != thisIndex) //don't send yourself your contribution
      pieces[designatedOwner].acceptBoundaryNodeContribution(node->lookupKey(), node->remoteIndex, node->moments);
    node->setType(Boundary);
  } else {
    node->numOwners = 1;
    node->setType(Internal);
    calculateRadiusFarthestCorner(node->moments, node->boundingBox);
  }
}

void TreePiece::acceptBoundaryNodeContribution(const Key lookupKey, const u_int64_t numParticles, const MultipoleMoments& moments) {
  NodeLookupType::iterator nodeIter = nodeLookup.find(lookupKey);
	
  if(nodeIter == nodeLookup.end()) {
    //	cerr << "TreePiece " << thisIndex << ": Well crap, how the hell did this happen?" << endl;
    //	cerr << "Key: " << keyBits(lookupKey, 63) << endl;
    pieces[thisIndex].acceptBoundaryNodeContribution(lookupKey,numParticles,moments); 
    //	cerr << "Zeroth particle: " << myParticles << endl;
    //	cerr << "leftBoundary: " << leftBoundary << endl;
    /*cerr << "My Left bound : " << keyBits(myParticles[0].key, 63) << endl;
      cerr << "My Left bound : " << keyBits(leftBoundary->key, 63) << endl;
      cerr << "My Right bound: " << keyBits(rightBoundary->key, 63) << endl;*/
    return;
  }
	
  SFCTreeNode* node = nodeIter->second;
  //merge new information
  node->remoteIndex += numParticles;
  node->moments += moments;
  //decrement number of contributions
  node->numOwners--;
  //if done, send final information to all co-owners
  if(node->numOwners == 1) {
    calculateRadiusFarthestCorner(node->moments, node->boundingBox);
    unsigned int firstOwner, lastOwner;
    //recalculate number of owners, get co-owners
    nodeOwnership(node, 0, &node->numOwners, &firstOwner, &lastOwner);
    //send moments, numParticles to all co-owners
    for(unsigned int i = firstOwner; i <= lastOwner; ++i)
      pieces[i].acceptBoundaryNode(lookupKey, node->remoteIndex, node->moments);
  }
}

void TreePiece::acceptBoundaryNode(const Key lookupKey, const u_int64_t numParticles, const MultipoleMoments& moments) {
  NodeLookupType::iterator nodeIter = nodeLookup.find(lookupKey);
	
  if(nodeIter == nodeLookup.end()) {
    cerr << "Well crap, how the hell did this happen, especially now?" << endl;
    return;
  }
	
  SFCTreeNode* node = nodeIter->second;
  if(node->getType() != Boundary)
    cerr << "How does this work?" << endl;
  //merge final information
  node->remoteIndex = numParticles;
  node->moments = moments;	
  boundaryNodesPending--;
  if(boundaryNodesPending == 0) {
    calculateRemoteMoments(root);
    contribute(0, 0, CkReduction::concat, callback);
  }
}

void TreePiece::calculateRemoteMoments(SFCTreeNode* node) {
  if(node->getType() == NonLocal) {
    SFCTreeNode* sibling = dynamic_cast<SFCTreeNode *>(node->getSibling());
    SFCTreeNode* parent = dynamic_cast<SFCTreeNode *>(node->parent);
    node->beginParticle = 0;
    if(sibling->getType() == Boundary)
      node->endParticle = parent->remoteIndex - sibling->remoteIndex;
    else
      node->endParticle = parent->remoteIndex - (sibling->endParticle - sibling->beginParticle);
    if(node->endParticle != 0) {
      node->moments = parent->moments - sibling->moments;
      calculateRadiusFarthestCorner(node->moments, node->boundingBox);
    } else {
      nodeLookup.erase(node->lookupKey());
      if(node->isLeftChild())
	parent->leftChild = 0;
      else
	parent->rightChild = 0;
      delete node;
    }
  } else if(node->getType() == Boundary) {
    if(node->leftChild)
      calculateRemoteMoments(dynamic_cast<SFCTreeNode *>(node->leftChild));
    if(node->rightChild)
      calculateRemoteMoments(dynamic_cast<SFCTreeNode *>(node->rightChild));
  }
}

void TreePiece::calculateGravityDirect(const CkCallback& cb) {
  callback = cb;
	
  GravityRequest req;
  req.requestingPieceIndex = thisIndex;
	
  myNumParticlesPending = myNumParticles * numTreePieces;
	
  for(u_int64_t i = 1; i <= myNumParticles; ++i) {
    req.identifier = i;
    req.soft = myParticles[i].soft;
    req.position = myParticles[i].position;
    myParticles[i].acceleration = 0;
    myParticles[i].potential = 0;
    pieces.fillRequestDirect(req);
  }
	
  started = true;
}

inline void
partForce(GravityParticle *part, GravityRequest& req)
{
  Vector3D<double> r;
  double rsq;
  double twoh, a, b;

  r = part->position - req.position;
  rsq = r.lengthSquared();
  twoh = part->soft + req.soft;
  if(rsq != 0) {
    SPLINE(rsq, twoh, a, b);
    req.acceleration += part->mass * r * b;
    req.potential -= part->mass * a;
  }
}

inline void
partBucketForce(GravityParticle *part, BucketGravityRequest& req)
{
  Vector3D<double> r;
  double rsq;
  double twoh, a, b;

	//SFCTreeNode* reqnode = bucketList[req.identifier];
	
  for(unsigned int j = 0; j < req.numParticlesInBucket; ++j) {
    r = part->position - req.positions[j];
    rsq = r.lengthSquared();
    twoh = part->soft + req.softs[j];
    if(rsq != 0) {
      SPLINE(rsq, twoh, a, b);
      req.accelerations[j] += part->mass * r * b;
			//myParticles[reqnode->beginParticle + j].acceleration += part->mass * r * b;
      req.potentials[j] -= part->mass * a;
    }
  }
}

inline void
nodeForce(GravityTreeNode *node, GravityRequest& req)
{
  Vector3D<double> r;
  double rsq;
  double twoh, a, b, c, d;
  MultipoleMoments m = node->moments;
    
  Vector3D<double> cm(m.cm);

  r = req.position - cm;
  rsq = r.lengthSquared();
  twoh = m.soft + req.soft;
  if(rsq != 0) {
    double dir = 1.0/sqrt(rsq);
    SPLINEQ(dir, rsq, twoh, a, b, c, d);
    double qirx = m.xx*r[0] + m.xy*r[1] + m.xz*r[2];
    double qiry = m.xy*r[0] + m.yy*r[1] + m.yz*r[2];
    double qirz = m.xz*r[0] + m.yz*r[1] + m.zz*r[2];
    double qir = 0.5*(qirx*r[0] + qiry*r[1] + qirz*r[2]);
    double tr = 0.5*(m.xx + m.yy + m.zz);
    double qir3 = b*m.totalMass + d*qir - c*tr;
    req.potential -= m.totalMass * a + c*qir - b*tr;
    req.acceleration[0] -= qir3*r[0] - c*qirx;
    req.acceleration[1] -= qir3*r[1] - c*qiry;
    req.acceleration[2] -= qir3*r[2] - c*qirz;
  }
}

inline void
nodeBucketForce(GravityTreeNode *node, BucketGravityRequest& req)
{
  Vector3D<double> r;
  double rsq;
  double twoh, a, b, c, d;
  MultipoleMoments m = node->moments;
    
  Vector3D<double> cm(m.cm);

	//SFCTreeNode* reqnode = bucketList[req.identifier];

  for(unsigned int j = 0; j < req.numParticlesInBucket; ++j) {
    r = req.positions[j] - cm;
    rsq = r.lengthSquared();
    twoh = m.soft + req.softs[j];
    if(rsq != 0) {
      double dir = 1.0/sqrt(rsq);
      SPLINEQ(dir, rsq, twoh, a, b, c, d);
      double qirx = m.xx*r[0] + m.xy*r[1] + m.xz*r[2];
      double qiry = m.xy*r[0] + m.yy*r[1] + m.yz*r[2];
      double qirz = m.xz*r[0] + m.yz*r[1] + m.zz*r[2];
      double qir = 0.5*(qirx*r[0] + qiry*r[1] + qirz*r[2]);
      double tr = 0.5*(m.xx + m.yy + m.zz);
      double qir3 = b*m.totalMass + d*qir - c*tr;
      req.potentials[j] -= m.totalMass * a + c*qir - b*tr;
      req.accelerations[j][0] -= qir3*r[0] - c*qirx;
      req.accelerations[j][1] -= qir3*r[1] - c*qiry;
      req.accelerations[j][2] -= qir3*r[2] - c*qirz;
    
			/******************ADDED**********************/
			//SFCTreeNode* reqnode = bucketList[req.identifier];

			//for(unsigned int i = reqnode->beginParticle; i < reqnode->endParticle; ++i){
			//myParticles[reqnode->beginParticle + j].acceleration[0] -= qir3*r[0] - c*qirx;
			//myParticles[reqnode->beginParticle + j].acceleration[1] -= qir3*r[1] - c*qiry;
			//myParticles[reqnode->beginParticle + j].acceleration[2] -= qir3*r[2] - c*qirz;
			//}

		}
  }
}

inline
bool TreePiece::openCriterion(GravityTreeNode *node,
			      GravityRequest& req)
{
  // Note that some of this could be pre-calculated into an "opening radius"
  Sphere<double> s(node->moments.cm,
		   opening_geometry_factor * node->moments.radius / theta);
  return Space::contains(s, req.position);
}

inline
bool TreePiece::openCriterionBucket(GravityTreeNode *node,
				    BucketGravityRequest& req)
{
  // Note that some of this could be pre-calculated into an "opening radius"
  Sphere<double> s(node->moments.cm,
		   opening_geometry_factor * node->moments.radius / theta);
  return Space::intersect(req.boundingBox, s);
}

void TreePiece::fillRequestDirect(GravityRequest req) {
  for(u_int64_t i = 1; i <= myNumParticles; ++i) {
    partForce(&myParticles[i], req);
  }
  streamingProxy[req.requestingPieceIndex].receiveGravityDirect(req);
}

void TreePiece::receiveGravityDirect(const GravityRequest& req) {
  myParticles[req.identifier].acceleration += req.acceleration;
  myParticles[req.identifier].potential += req.potential;
  if(started && --myNumParticlesPending == 0) {
    started = false;
    contribute(0, 0, CkReduction::concat, callback);
  }
}

void TreePiece::startNextParticle() {
  if(nextParticle > myNumParticles)
    return;
  GravityRequest req;
  req.startingNode = root->lookupKey();
  req.requestingPieceIndex = thisIndex;
  req.identifier = nextParticle;
  req.soft = myParticles[nextParticle].soft;
  req.position = myParticles[nextParticle].position;
  myParticles[nextParticle].treeAcceleration = 0;
  streamingProxy[thisIndex].fillRequestTree(req);
  nextParticle++;
}

void TreePiece::calculateGravityTree(double t, const CkCallback& cb) {
  callback = cb;
  theta = t;
  mySerialNumber = 0;
  myNumCellInteractions = 0;
  myNumParticleInteractions = 0;
  myNumMACChecks = 0;
  myNumProxyCalls = 0;
  myNumProxyCallsBack = 0;
	
  nextParticle = 1;
  startNextParticle();
	
  myNumParticlesPending = myNumParticles;
  started = true;
	
}

void TreePiece::fillRequestTree(GravityRequest req) {
  //double startTime = CkWallTimer();
  //lookup starting node using startingNode key
  NodeLookupType::iterator nodeIter = nodeLookup.find(req.startingNode);
  if(nodeIter == nodeLookup.end()) {
    cerr << "Well crap, how the hell did this happen here?" << endl;
    return;
  }
  SFCTreeNode* node = nodeIter->second;
	
  //make the request ready to go in the queue
  req.numAdditionalRequests = 1;
  req.acceleration = 0;
  req.potential = 0;
  req.numCellInteractions = 0;
  req.numParticleInteractions = 0;
  req.numMACChecks = 0;
  req.numEntryCalls = 0;
	
  //enter the requests into the queue
  unfilledRequests[mySerialNumber] = req;
	
  // make this request ready to get sent to other pieces
  req.requestingPieceIndex = thisIndex;
  req.identifier = mySerialNumber;
		
  walkTree(node, req);
	
  receiveGravityTree(req);
	
  mySerialNumber++;

  startNextParticle();
		
  //CkPrintf("%g\t%d\t%d\t%d\n", CkWallTimer() - startTime, req.numMACChecks, req.numCellInteractions, req.numParticleInteractions);
  //cout << (CkWallTimer() - startTime) << '\t' << req.numMACChecks << '\t' << req.numCellInteractions << '\t' << req.numParticleInteractions << '\n';
}

void TreePiece::walkTree(GravityTreeNode* node, GravityRequest& req) {
  req.numMACChecks++;
  myNumMACChecks++;
  if(!openCriterion(node, req)) {
    req.numCellInteractions++;
    myNumCellInteractions++;
    nodeForce(node, req);
  } else if(node->getType() == Bucket) {
    req.numParticleInteractions += node->endParticle - node->beginParticle;
    myNumParticleInteractions += node->endParticle - node->beginParticle;
    for(unsigned int i = node->beginParticle; i < node->endParticle; ++i) {
      partForce(&myParticles[i], req);
    }
  } else if(node->getType() == NonLocal) {
    unfilledRequests[mySerialNumber].numAdditionalRequests++;
    req.numEntryCalls++;
    req.startingNode = dynamic_cast<SFCTreeNode *>(node)->lookupKey();
    streamingProxy[node->remoteIndex].fillRequestTree(req);
    myNumProxyCalls++;
  } else {
    GenericTreeNode** childrenIterator = node->getChildren();
    for(unsigned int i = 0; i < node->numChildren(); ++i) {
      if(childrenIterator[i])
	walkTree(dynamic_cast<GravityTreeNode *>(childrenIterator[i]), req);
    }
  }
}

void TreePiece::receiveGravityTree(const GravityRequest& req) {
  //lookup request
  UnfilledRequestsType::iterator requestIter = unfilledRequests.find(req.identifier);
  if(requestIter == unfilledRequests.end()) {
    cerr << "Well crap, how the hell did this happen here and now?" << endl;
    cerr << "TreePiece " << thisIndex << ": Got request from " << req.requestingPieceIndex << " with id " << req.identifier << endl;
    return;
  }
  GravityRequest& request = requestIter->second;
  request.merge(req);
  if(--request.numAdditionalRequests == 0) {
    if((int) request.requestingPieceIndex == thisIndex) {
      myNumParticlesPending--;
      //this request originated here, it's for one of my particles
      myParticles[request.identifier].update(request);
    } else {
      streamingProxy[request.requestingPieceIndex].receiveGravityTree(request);
      myNumProxyCallsBack++;
    }
		
    unfilledRequests.erase(requestIter);
    if(started && myNumParticlesPending == 0) {
      started = false;
      contribute(0, 0, CkReduction::concat, callback);
      cout << "TreePiece " << thisIndex << ": Made " << myNumProxyCalls << " proxy calls forward, " << myNumProxyCallsBack << " to respond" << endl;
      if(verbosity > 4)
	cerr << "TreePiece " << thisIndex << ": My particles are done" << endl;
    }
  }
}

#ifdef SEND_VERSION
void TreePiece::startNextBucket() {
  if(currentBucket >= numBuckets)
    return;
	
  SFCTreeNode* node = bucketList[currentBucket++];
  unsigned int numParticlesInBucket = node->endParticle - node->beginParticle;
  BucketGravityRequest req(numParticlesInBucket);
  req.startingNode = root->lookupKey();
  req.identifier = node->beginParticle;
  req.requestingPieceIndex = thisIndex;
  for(unsigned int i = node->beginParticle; i < node->endParticle; ++i) {
    req.softs[i - node->beginParticle] = myParticles[i].soft;
    req.positions[i - node->beginParticle] = myParticles[i].position;
    req.boundingBox.grow(myParticles[i].position);
    myParticles[i].treeAcceleration = 0;
  }
  streamingProxy[thisIndex].fillRequestBucketTree(req);
  myNumProxyCalls++;
}
#else
void TreePiece::startNextBucket() {
  if(currentBucket >= numBuckets)
    return;
	
  SFCTreeNode* node = bucketList[currentBucket];
  unsigned int numParticlesInBucket = node->endParticle - node->beginParticle;

  BucketGravityRequest req(numParticlesInBucket);
  req.startingNode = root->lookupKey();
  req.identifier = currentBucket;
  req.requestingPieceIndex = thisIndex;
  for(unsigned int i = node->beginParticle; i < node->endParticle; ++i) {
    req.softs[i - node->beginParticle] = myParticles[i].soft;
    req.positions[i - node->beginParticle] = myParticles[i].position;
    req.potentials[i - node->beginParticle] = 0;
    req.boundingBox.grow(myParticles[i].position);
    myParticles[i].treeAcceleration = 0;
  }
  req.finished = 0;
  bucketReqs[currentBucket] = req;
  walkBucketTree(root, bucketReqs[currentBucket]);
  bucketReqs[currentBucket].finished = 1;
  finishBucket(currentBucket);
  //	currentBucket++;
  //	startNextBucket();
}

void TreePiece::finishBucket(int iBucket) {
  BucketGravityRequest *req = &bucketReqs[iBucket];


  if(req->finished && req->numAdditionalRequests == 0) {
    myNumParticlesPending -= req->numParticlesInBucket;
    int iStart = bucketList[iBucket]->beginParticle;
    for(unsigned int i = 0; i < req->numParticlesInBucket; ++i) {
      myParticles[iStart + i].treeAcceleration
	+= req->accelerations[i];
      myParticles[iStart + i].potential
	+= req->potentials[i];
    }
    if(started && myNumParticlesPending == 0) {
      started = false;
      contribute(0, 0, CkReduction::concat, callback);
      /*   cout << "TreePiece " << thisIndex << ": Made " << myNumProxyCalls
	   << " proxy calls forward, " << myNumProxyCallsBack
	   << " to respond in finishBucket" << endl;*/
      if(verbosity)
				CkPrintf("[%d] TreePiece %d finished with bucket %d \n",CkMyPe(),thisIndex,iBucket);
      if(verbosity > 4)
	cerr << "TreePiece " << thisIndex << ": My particles are done"
	     << endl;
    }
  }
}

#endif

void TreePiece::doAllBuckets(){
  if(thisIndex == 2){
    char fout[100];
    sprintf(fout,"tree.%d.%d",thisIndex,iterationNo);
    ofstream ofs(fout);
    printTree(root,ofs);
    ofs.close();
  }
  /*for(;currentBucket <numBuckets;currentBucket++){
    startNextBucket();
    if(currentBucket%YIELDPERIOD == YIELDPERIOD -1 ){
      CthYield();
      }	
  }*/
  dummyMsg *msg = new (32) dummyMsg;
  *((int *)CkPriorityPtr(msg))=10*(1+thisIndex);
  CkSetQueueing(msg,CK_QUEUEING_IFIFO);
  msg->val=0;
  thisProxy[thisIndex].nextBucket(msg);
}

void TreePiece::nextBucket(dummyMsg *msg){
  int i=0;
  while(i<_yieldPeriod && currentBucket<numBuckets){
    startNextBucket();
    currentBucket++;
    i++;
  }
  if(currentBucket<numBuckets){
    thisProxy[thisIndex].nextBucket(msg);
  }
}

void TreePiece::calculateGravityBucketTree(double t, const CkCallback& cb) {
  callback = cb;
  theta = t;
  mySerialNumber = 0;
  myNumProxyCalls = 0;
  myNumProxyCallsBack = 0;
  myNumCellInteractions=myNumParticleInteractions=myNumMACChecks=0;
  cachecellcount=0;
  iterationNo++;
  if(localCache == NULL){
    localCache = cacheManagerProxy.ckLocalBranch();
  }
  localCache->cacheSync(iterationNo);
  if(verbosity)
    CkPrintf("TreePiece %d: I have %d buckets\n",thisIndex,numBuckets);
	
  bucketReqs = new BucketGravityRequest[numBuckets];
	
  currentBucket = 0;
  myNumParticlesPending = myNumParticles;
  started = true;
  countIntersects=0;
  countHits=0;
  //	startNextBucket();
  doAllBuckets();
}

void TreePiece::fillRequestBucketTree(BucketGravityRequest req) {
  NodeLookupType::iterator nodeIter = nodeLookup.find(req.startingNode);
  if(nodeIter == nodeLookup.end()) {
    cerr << "Well crap, how the hell did this happen here?" << endl;
    return;
  }
  SFCTreeNode* node = nodeIter->second;
	
  //make the request ready to go in the queue
  req.numAdditionalRequests = 1;
  for(unsigned int i = 0; i < req.numParticlesInBucket; ++i) {
    req.accelerations[i] = 0;
    req.potentials[i] = 0;
  }
	
  //enter the requests into the queue
  unfilledBucketRequests[mySerialNumber] = req;
	
  // make this request ready to get sent to other pieces
  req.requestingPieceIndex = thisIndex;
  req.identifier = mySerialNumber;
		
  walkBucketTree(node, req);
	
  receiveGravityBucketTree(req);
	
  startNextBucket();
	
  mySerialNumber++;
}

void TreePiece::startlb(CkCallback &cb){
  callback = cb;
	if(verbosity > 1)
	CkPrintf("[%d] TreePiece %d calling AtSync()\n",CkMyPe(),thisIndex);
  AtSync();
}

void TreePiece::ResumeFromSync(){
	if(verbosity > 1)
	CkPrintf("[%d] TreePiece %d in ResumefromSync\n",CkMyPe(),thisIndex);
  contribute(0, 0, CkReduction::concat, callback);
}

void TreePiece::lookupNode(Key lookupKey,SFCTreeNode *res){
  SFCTreeNode* node = nodeLookup[lookupKey];
  if(node != NULL){
    copySFCTreeNode(*res,node);
  }else{
    res->setType(Empty);
  }
};

#ifdef SEND_VERSION
/*
 * "Send" version of Treewalk.
 * When off non-local data is need the tree walk is continued on the
 * appropriate remote node.
 */
void TreePiece::walkBucketTree(GravityTreeNode* node, BucketGravityRequest& req) {
  myNumMACChecks++;
  if(!openCriterionBucket(node, req)) {
    myNumCellInteractions += req.numParticlesInBucket;
    nodeBucketForce(node, req);
  } else if(node->getType() == Bucket) {
    myNumParticleInteractions += req.numParticlesInBucket * (node->endParticle - node->beginParticle);
    for(unsigned int i = node->beginParticle; i < node->endParticle; ++i) {
      partBucketForce(&myParticles[i], req);
    }
  } else if(node->getType() == NonLocal) {
    unfilledBucketRequests[mySerialNumber].numAdditionalRequests++;
    req.startingNode = dynamic_cast<SFCTreeNode *>(node)->lookupKey();
    streamingProxy[node->remoteIndex].fillRequestBucketTree(req);
    myNumProxyCalls++;
  } else {
    GenericTreeNode** childrenIterator = node->getChildren();
    for(unsigned int i = 0; i < node->numChildren(); ++i) {
      if(childrenIterator[i])
	walkBucketTree(dynamic_cast<GravityTreeNode *>(childrenIterator[i]), req);
    }
  }
}
#else

/*
 * For cached version we have 2 walks: one for on processor and one
 * that hits the cache.
 * When remote data is needed we go to the second version.
 */
void TreePiece::walkBucketTree(GravityTreeNode* node, BucketGravityRequest& req) {
	myNumMACChecks++;
	Key lookup = dynamic_cast<SFCTreeNode *>(node)->lookupKey();
  Vector3D<double> cm(node->moments.cm);
  Vector3D<double> r;
  Sphere<double> s(cm, opening_geometry_factor * node->moments.radius / theta);
  if(!openCriterionBucket(node, req)) {
    countIntersects++;
			Key lookupKey = dynamic_cast<SFCTreeNode *>(node)->lookupKey();
    myNumCellInteractions += req.numParticlesInBucket;
		/**********************************/
		MultipoleMoments m = node->moments;
		
		SFCTreeNode* reqnode = bucketList[req.identifier];
		//unsigned int numParticlesInBucket = reqnode->endParticle - reqnode->beginParticle;
  	/***************************/
		for(unsigned int i = reqnode->beginParticle; i < reqnode->endParticle; ++i)
			myParticles[i].intcellmass += m.totalMass;
		/**********************************/	
    nodeBucketForce(node, req);
  } else if(node->getType() == Bucket) {
    myNumParticleInteractions += req.numParticlesInBucket * (node->endParticle - node->beginParticle);
    SFCTreeNode* reqnode = bucketList[req.identifier];
		for(unsigned int i = node->beginParticle; i < node->endParticle; ++i) {
  		/***************************/
			for(unsigned int j = reqnode->beginParticle; j < reqnode->endParticle; ++j) {
    			myParticles[j].intpartmass += myParticles[i].mass;
			}
			/****************************/
			partBucketForce(&myParticles[i], req);
    }
  } else if(node->getType() == NonLocal) {
    Key lookupKey = dynamic_cast<SFCTreeNode *>(node)->lookupKey();
    // Use cachedWalkBucketTree() as callback
    GravityTreeNode *pnode = requestNode(node->remoteIndex, lookupKey, req);
    if(pnode) {
      countHits++;
      cachedWalkBucketTree(pnode, req);
    }
  } else {
    GenericTreeNode** childrenIterator = node->getChildren();
    for(unsigned int i = 0; i < node->numChildren(); ++i) {
      if(childrenIterator[i])
				walkBucketTree(dynamic_cast<GravityTreeNode *>(childrenIterator[i]), req);
    }
  }
}

/*
 * Cached version of Tree walk:
 */
void TreePiece::cachedWalkBucketTree(GravityTreeNode* node, BucketGravityRequest& req) {
  myNumMACChecks++;
	Key lookup = dynamic_cast<SFCTreeNode *>(node)->lookupKey();
		
  assert(node->getType() != Invalid);
	
	/***********BUG FIX**************/
	if(node->getType() == Empty){
      return;
  }
	/********************************/
	
  if(!openCriterionBucket(node, req)) {
		
		myNumCellInteractions += req.numParticlesInBucket;
		cachecellcount+=req.numParticlesInBucket;
   	MultipoleMoments m = node->moments;
		SFCTreeNode* reqnode = bucketList[req.identifier];
		for(unsigned int i = reqnode->beginParticle; i < reqnode->endParticle; ++i)
			myParticles[i].extcellmass += m.totalMass;
		nodeBucketForce(node, req);
  } else if(node->getType() == Bucket) {
    /*
     * Sending the request for all the particles at one go, instead of one by one
     */
    Key lookupKey = dynamic_cast<SFCTreeNode *>(node)->lookupKey();
    GravityParticle *part = requestParticles(lookupKey,node->remoteIndex,node->beginParticle,node->endParticle,req);
    if(part != NULL){
      myNumParticleInteractions += req.numParticlesInBucket * (node->endParticle - node->beginParticle);
      SFCTreeNode* reqnode = bucketList[req.identifier];

			for(unsigned int i = node->beginParticle; i < node->endParticle; ++i) {
				for(unsigned int j = reqnode->beginParticle; j < reqnode->endParticle; ++j) {
    			myParticles[j].extpartmass += myParticles[i].mass;
				}
				partBucketForce(&part[i-node->beginParticle], req);
      }
    }	
  } else if(node->getType() == NonLocal) {
    Key lookupKey = dynamic_cast<SFCTreeNode *>(node)->lookupKey();
    // Use cachedWalkBucketTree() as callback
    GravityTreeNode *pnode = requestNode(node->remoteIndex, lookupKey, req);
    if(pnode) {
      cachedWalkBucketTree(pnode, req);
    }
  }
  else {
    //		assert(node->getType() != Empty);
    /*if(node->getType() == Empty){
      return;
    }*/
			
    // Since this is in the cache, getting at the children
    // is non-trivial.  Here I assume left and right
    // nodes.  I'm not sure how to do this generically.
    // 
    Key lookupKey = dynamic_cast<SFCTreeNode *>(node)->leftChildLookupKey();
    // Use cachedWalkBucketTree() as callback
    GravityTreeNode* child = requestNode(node->remoteIndex,
					 lookupKey, req);
    if(child) {
      cachedWalkBucketTree(child, req);
    }
    else { //missed the cache
    }
    lookupKey = dynamic_cast<SFCTreeNode *>(node)->rightChildLookupKey();
    // Use cachedWalkBucketTree() as callback
    child = requestNode(node->remoteIndex, lookupKey, req);
    if(child) {
      cachedWalkBucketTree(child, req);
    }
    else { // missed the cache
    }
  }
}

SFCTreeNode* TreePiece::requestNode(int remoteIndex, Key lookupKey,
				    BucketGravityRequest& req)
{
  // Call proxy on remote node
  assert(remoteIndex < (int) numTreePieces);
  //in the current form it is possible   
  //   assert(remoteIndex != thisIndex);
  if(_cache){	
    if(localCache == NULL){
      localCache = cacheManagerProxy.ckLocalBranch();
    }
    SFCTreeNode *res=localCache->requestNode(thisIndex,remoteIndex,lookupKey,&req);
    if(!res){
      req.numAdditionalRequests++;
      myNumProxyCalls++;
    }
    return res;
  }
  else{	
    req.numAdditionalRequests++;
    streamingProxy[remoteIndex].fillRequestNode(thisIndex, lookupKey, req);
    myNumProxyCalls++;
    return NULL; 
  }
}


/*
  copy the data from node to tmp
*/
void TreePiece::copySFCTreeNode(SFCTreeNode &tmp,SFCTreeNode *node){
  if(node == NULL){
    tmp.setType(Empty);
    return;
  }
  tmp.setType(node->getType());
  tmp.moments = node->moments;
  tmp.beginParticle = node->beginParticle;
  tmp.endParticle = node->endParticle;
  tmp.remoteIndex = node->remoteIndex;
  tmp.key = node->key;
  tmp.level = node->level;

  assert(tmp.getType() != Invalid);
	
  if(tmp.getType() == Boundary || tmp.getType() == Internal
     || tmp.getType() == Bucket)
    tmp.remoteIndex = thisIndex;
	
}
/*
  do a prefix traversal starting at node and copy the keys and nodes into 
  the passed arrays
*/
void TreePiece::prefixCopyNode(SFCTreeNode *node,Key lookupKey,Key *cacheKeys,SFCTreeNode *cacheNodes,int *count,int depth){
  if(depth >= _cacheLineDepth){
    return;
  }
  copySFCTreeNode(cacheNodes[*count],node);
  if(node != NULL){
    assert(lookupKey == node->lookupKey());
    cacheKeys[*count] = node->lookupKey();
  }else{
    cacheKeys[*count] = lookupKey;
  }
  (*count)++;
  if(node == NULL){
    return;
  }
  prefixCopyNode(nodeLookup[node->leftChildLookupKey()],node->leftChildLookupKey(),cacheKeys,cacheNodes,count,depth+1);
  prefixCopyNode(nodeLookup[node->rightChildLookupKey()],node->rightChildLookupKey(),cacheKeys,cacheNodes,count,depth+1);
}

void TreePiece::fillRequestNode(int retIndex, Key lookupKey,
				BucketGravityRequest& req)
{
  SFCTreeNode* node = nodeLookup[lookupKey];
  SFCTreeNode tmp;
  if(node != NULL) {
    if(_cache){
      int number = (1<<_cacheLineDepth)-1;
      Key *cacheKeys = new Key[number];
      SFCTreeNode *cacheNodes = new SFCTreeNode[number];
      int count=0;
      prefixCopyNode(node,lookupKey,cacheKeys,cacheNodes,&count,0);
      //	cacheManagerProxy[retIndex].recvNodes(lookupKey,thisIndex,tmp);
      cacheManagerProxy[retIndex].recvNodes(count,cacheKeys,cacheNodes,thisIndex);
		
      delete [] cacheKeys;
      delete [] cacheNodes;
    }else{
      copySFCTreeNode(tmp,node);
      streamingProxy[retIndex].receiveNode(tmp,req);
    }
  }
  else {	// Handle NULL nodes
    tmp.setType(Empty);
    if(_cache){
      cacheManagerProxy[retIndex].recvNodes(lookupKey,thisIndex,tmp);
    }else{	
      streamingProxy[retIndex].receiveNode(tmp, req);
    }	
  }
}

void TreePiece::receiveNode(SFCTreeNode node, BucketGravityRequest& req)
{
  bucketReqs[req.identifier].numAdditionalRequests--;
  assert(node.getType() != Invalid);
  if(node.getType() != Empty)	{ // Node could be NULL
    assert((int) node.remoteIndex != thisIndex);
    cachedWalkBucketTree(&node, bucketReqs[req.identifier]);
  }else{
  }
    
  finishBucket(req.identifier);
}

void TreePiece::receiveNode_inline(SFCTreeNode node, BucketGravityRequest& req){
        receiveNode(node,req);
}
/*
  This function is not used anymore. It is extremely inefficient
  to request each particle in a node individually instead of requesting all
  the particles in a node as done in requestParticles defined below.
*/
GravityParticle* TreePiece::requestParticle(int remoteIndex, int iPart,
					    BucketGravityRequest& req)
{
  assert(remoteIndex < (int) numTreePieces);
  // Call proxy on remote node
  req.numAdditionalRequests++;
  myNumProxyCalls++;

  streamingProxy[remoteIndex].fillRequestParticle(thisIndex, iPart, req);
    
  return NULL; // If we actually had a cache, this might return something
}

GravityParticle *TreePiece::requestParticles(Key &key,int remoteIndex,int begin,int end,BucketGravityRequest& req){
  if(_cache){
    if(localCache == NULL){
      localCache = cacheManagerProxy.ckLocalBranch();
    }
    GravityParticle *p = localCache->requestParticles(thisIndex,key,remoteIndex,begin,end,&req);
    if(!p ){
      req.numAdditionalRequests += end-begin;
    }
    return p;
  }else{
    req.numAdditionalRequests += end-begin;
    myNumProxyCalls++;
	
    streamingProxy[remoteIndex].fillRequestParticles(key,thisIndex,begin,end,req);
    return NULL;
  }
};

void TreePiece::fillRequestParticle(int retIndex, int iPart,
				    BucketGravityRequest& req)
{
  assert(retIndex < (int) numTreePieces);
  streamingProxy[retIndex].receiveParticle(myParticles[iPart], req);
}

void TreePiece::fillRequestParticles(Key key,int retIndex, int begin,int end,
				     BucketGravityRequest& req)
{
  if(_cache){
    cacheManagerProxy[retIndex].recvParticles(key,&myParticles[begin],end-begin,thisIndex);
  }else{
    streamingProxy[retIndex].receiveParticles(&myParticles[begin], end-begin,req);
  }	
}



void TreePiece::receiveParticle(GravityParticle part,
				BucketGravityRequest& req)
{
  bucketReqs[req.identifier].numAdditionalRequests--;
  myNumParticleInteractions += bucketReqs[req.identifier].numParticlesInBucket;
  partBucketForce(&part, bucketReqs[req.identifier]);
  finishBucket(req.identifier);
}

void TreePiece::receiveParticles(GravityParticle *part,int num,
				 BucketGravityRequest& req)
{
  bucketReqs[req.identifier].numAdditionalRequests -= num;
  myNumParticleInteractions += bucketReqs[req.identifier].numParticlesInBucket * num;
  SFCTreeNode* reqnode = bucketList[req.identifier];

	for(int i=0;i<num;i++){
    for(unsigned int j = reqnode->beginParticle; j < reqnode->endParticle; ++j) {
    			myParticles[j].extpartmass += part[i].mass;
		}

		partBucketForce(&part[i], bucketReqs[req.identifier]);
  }		
  finishBucket(req.identifier);
}

void TreePiece::receiveParticles_inline(GravityParticle *part,int num,
                                 BucketGravityRequest& req){
        receiveParticles(part,num,req);
}


#endif

void TreePiece::receiveGravityBucketTree(const BucketGravityRequest& req) {
  //lookup request
  UnfilledBucketRequestsType::iterator requestIter = unfilledBucketRequests.find(req.identifier);
  if(requestIter == unfilledBucketRequests.end()) {
    cerr << "Well crap, how the hell did this happen here and now?" << endl;
    cerr << "TreePiece " << thisIndex << ": Got request from " << req.requestingPieceIndex << " with id " << req.identifier << endl;
    return;
  }
  BucketGravityRequest& request = requestIter->second;
  if(request.numParticlesInBucket != req.numParticlesInBucket)
    cerr << "How could this be?" << endl;
  request.merge(req);
  if(--request.numAdditionalRequests == 0) {
    if((int) request.requestingPieceIndex == thisIndex) {
      myNumParticlesPending -= request.numParticlesInBucket;
      //this request originated here, it's for one of my particles
      for(unsigned int i = 0; i < request.numParticlesInBucket; ++i) {
	myParticles[request.identifier + i].treeAcceleration += request.accelerations[i];
			    
	myParticles[request.identifier + i].potential += request.potentials[i];
      }
    } else {
      streamingProxy[request.requestingPieceIndex].receiveGravityBucketTree(request);
      myNumProxyCallsBack++;
    }
		
    unfilledBucketRequests.erase(requestIter);
    if(started && myNumParticlesPending == 0) {
      started = false;
      contribute(0, 0, CkReduction::concat, callback);
      cout << "TreePiece " << thisIndex << ": Made " << myNumProxyCalls << " proxy calls forward, " << myNumProxyCallsBack << " to respond in receiveGravityBucketTree" << endl;
      if(verbosity > 4)
	cerr << "TreePiece " << thisIndex << ": My particles are done" << endl;
    }
  }
}

void TreePiece::outputAccelerations(OrientedBox<double> accelerationBox, const string& suffix, const CkCallback& cb) {
  if(thisIndex == 0) {
    if(verbosity > 2)
      cerr << "TreePiece " << thisIndex << ": Writing header for accelerations file" << endl;
    FILE* outfile = fopen((basefilename + "." + suffix).c_str(), "wb");
    XDR xdrs;
    xdrstdio_create(&xdrs, outfile, XDR_ENCODE);
    fh.code = float64;
    fh.dimensions = 3;
    if(!xdr_template(&xdrs, &fh) || !xdr_template(&xdrs, &accelerationBox.lesser_corner) || !xdr_template(&xdrs, &accelerationBox.greater_corner)) {
      cerr << "TreePiece " << thisIndex << ": Could not write header to accelerations file, aborting" << endl;
      CkAbort("Badness");
    }
    xdr_destroy(&xdrs);
    fclose(outfile);
  }
	
  if(verbosity > 3)
    cerr << "TreePiece " << thisIndex << ": Writing my accelerations to disk" << endl;
	
  FILE* outfile = fopen((basefilename + "." + suffix).c_str(), "r+b");
  fseek(outfile, 0, SEEK_END);
  XDR xdrs;
  xdrstdio_create(&xdrs, outfile, XDR_ENCODE);
	
  for(unsigned int i = 1; i <= myNumParticles; ++i) {
    accelerationBox.grow(myParticles[i].acceleration);
    if(!xdr_template(&xdrs, &(myParticles[i].acceleration))) {
      cerr << "TreePiece " << thisIndex << ": Error writing accelerations to disk, aborting" << endl;
      CkAbort("Badness");
    }
  }
	
  if(thisIndex == (int) numTreePieces - 1) {
    if(!xdr_setpos(&xdrs, FieldHeader::sizeBytes) || !xdr_template(&xdrs, &accelerationBox.lesser_corner) || !xdr_template(&xdrs, &accelerationBox.greater_corner)) {
      cerr << "TreePiece " << thisIndex << ": Error going back to write the acceleration bounds, aborting" << endl;
      CkAbort("Badness");
    }
    if(verbosity > 2)
      cerr << "TreePiece " << thisIndex << ": Wrote the acceleration bounds" << endl;
    cb.send();
  }
	
  xdr_destroy(&xdrs);
  fclose(outfile);
	
  if(thisIndex != (int) numTreePieces - 1)
    pieces[thisIndex + 1].outputAccelerations(accelerationBox, suffix, cb);
}

/****************************ADDED***********************************/

void TreePiece::outputAccASCII(OrientedBox<double> accelerationBox, const string& suffix, const CkCallback& cb) {
  if((thisIndex==0 && packed) || (thisIndex==0 && !packed && cnt==0)) {
    if(verbosity > 2)
      cerr << "TreePiece " << thisIndex << ": Writing header for accelerations file" << endl;
    FILE* outfile = fopen((basefilename + "." + suffix).c_str(), "wb");
		fprintf(outfile,"%d\n",fh.numParticles);
    fclose(outfile);
  }
	
	/*if(thisIndex==0) {
    if(verbosity > 2)
      cerr << "TreePiece " << thisIndex << ": Writing header for accelerations file" << endl;
    FILE* outfile = fopen((basefilename + "." + suffix).c_str(), "wb");
		fprintf(outfile,"%d\n",fh.numParticles);
    fclose(outfile);
  }*/

  if(verbosity > 3)
    cerr << "TreePiece " << thisIndex << ": Writing my accelerations to disk" << endl;
	
  FILE* outfile = fopen((basefilename + "." + suffix).c_str(), "r+b");
  fseek(outfile, 0, SEEK_END);
	
  for(unsigned int i = 1; i <= myNumParticles; ++i) {
    accelerationBox.grow(myParticles[i].treeAcceleration);
		Vector3D<double> acc = (myParticles[i].treeAcceleration);
		double val=0.0;
		if(!packed){
			if(cnt==0)
				val=acc.x;
			if(cnt==1)
				val=acc.y;
			if(cnt==2)
				val=acc.z;
		}
		switch(packed){
		case 1:
    if(fprintf(outfile,"%.14g\n",acc.x) < 0) {
      cerr << "TreePiece " << thisIndex << ": Error writing accelerations to disk, aborting" << endl;
      CkAbort("Badness");
    }
  	if(fprintf(outfile,"%.14g\n",acc.y) < 0) {
      cerr << "TreePiece " << thisIndex << ": Error writing accelerations to disk, aborting" << endl;
      CkAbort("Badness");
    }
		if(fprintf(outfile,"%.14g\n",acc.z) < 0) {
      cerr << "TreePiece " << thisIndex << ": Error writing accelerations to disk, aborting" << endl;
      CkAbort("Badness");
    }
		break;
		case 0:
		if(fprintf(outfile,"%.14g\n",val) < 0) {
      cerr << "TreePiece " << thisIndex << ": Error writing accelerations to disk, aborting" << endl;
      CkAbort("Badness");
    }
		break;
		}
	}
	cnt++;
  /*if(thisIndex==(int)numTreePieces-1) {
    cb.send();
  }*/

	if((thisIndex==(int)numTreePieces-1 && packed) || (thisIndex==(int)numTreePieces-1 && !packed && cnt==3)) {
    cb.send();
  }
  fclose(outfile);
	
	if(thisIndex==(int)numTreePieces-1 && !packed && cnt<3)
		pieces[0].outputAccASCII(accelerationBox, suffix, cb);
		
  if(thisIndex!=(int)numTreePieces-1)
    pieces[thisIndex + 1].outputAccASCII(accelerationBox, suffix, cb);
	
}
/********************************************************************/

void TreePiece::outputStatistics(Interval<unsigned int> macInterval, Interval<unsigned int> cellInterval, Interval<unsigned int> particleInterval, Interval<unsigned int> callsInterval, double totalmass, const CkCallback& cb) {
  if(verbosity > 1) {
    cerr << "TreePiece " << thisIndex << ": Statistics\nMy number of MAC checks: " << myNumMACChecks << endl;
    cerr << "My number of particle-cell interactions: "
	 << myNumCellInteractions << " Per particle: "
	 << myNumCellInteractions/(double) myNumParticles
	 << "\nCache cell interactions count: " << cachecellcount << endl;
    cerr << "My number of particle-particle interactions: "
	 << myNumParticleInteractions << " Per Particle: "
	 << myNumParticleInteractions/(double) myNumParticles
	 << endl;
  }
	
/*	double calmass,prevmass;

	for(int i=1;i<=myNumParticles;i++){
		calmass = (myParticles[i].intcellmass + myParticles[i].intpartmass + myParticles[i].extcellmass + myParticles[i].extpartmass);
		if(i>1)
			prevmass = (myParticles[i-1].intcellmass + myParticles[i-1].intpartmass + myParticles[i-1].extcellmass + myParticles[i-1].extpartmass);
		//CkPrintf("treepiece:%d ,mass:%lf, totalmass:%lf\n",thisIndex,calmass,totalmass);
		if(i>1)
			if(calmass != prevmass)
				CkPrintf("Tree piece:%d -- particles %d and %d differ in calculated total mass\n",thisIndex,i-1,i);
		if(calmass != totalmass)
				CkPrintf("Tree piece:%d -- particle %d differs from total mass\n",thisIndex,i);
	}

	CkPrintf("TreePiece:%d everything seems ok..\n",thisIndex);
  */
	
  if(thisIndex == 0) {
    macInterval.max = 0;
    macInterval.min = macInterval.max - 1;
    cellInterval = macInterval;
    particleInterval = macInterval;
    callsInterval = macInterval;
		
    if(verbosity > 2)
      cerr << "TreePiece " << thisIndex << ": Writing headers for statistics files" << endl;
    fh.dimensions = 1;
    fh.code = TypeHandling::uint32;
    FILE* outfile = fopen((basefilename + ".MACs").c_str(), "wb");
    XDR xdrs;
    xdrstdio_create(&xdrs, outfile, XDR_ENCODE);
		
    unsigned int dummy;
    if(!xdr_template(&xdrs, &fh) || !xdr_template(&xdrs, &dummy) || !xdr_template(&xdrs, &dummy)) {
      cerr << "TreePiece " << thisIndex << ": Could not write header to MAC file, aborting" << endl;
      CkAbort("Badness");
    }
    xdr_destroy(&xdrs);
    fclose(outfile);
		
    outfile = fopen((basefilename + ".cellints").c_str(), "wb");
    xdrstdio_create(&xdrs, outfile, XDR_ENCODE);
    if(!xdr_template(&xdrs, &fh) || !xdr_template(&xdrs, &dummy) || !xdr_template(&xdrs, &dummy)) {
      cerr << "TreePiece " << thisIndex << ": Could not write header to cell-interactions file, aborting" << endl;
      CkAbort("Badness");
    }
    xdr_destroy(&xdrs);
    fclose(outfile);

    outfile = fopen((basefilename + ".partints").c_str(), "wb");
    xdrstdio_create(&xdrs, outfile, XDR_ENCODE);
    if(!xdr_template(&xdrs, &fh) || !xdr_template(&xdrs, &dummy) || !xdr_template(&xdrs, &dummy)) {
      cerr << "TreePiece " << thisIndex << ": Could not write header to particle-interactions file, aborting" << endl;
      CkAbort("Badness");
    }
    xdr_destroy(&xdrs);
    fclose(outfile);

    outfile = fopen((basefilename + ".calls").c_str(), "wb");
    xdrstdio_create(&xdrs, outfile, XDR_ENCODE);
    if(!xdr_template(&xdrs, &fh) || !xdr_template(&xdrs, &dummy) || !xdr_template(&xdrs, &dummy)) {
      cerr << "TreePiece " << thisIndex << ": Could not write header to entry-point calls file, aborting" << endl;
      CkAbort("Badness");
    }
    xdr_destroy(&xdrs);
    fclose(outfile);
  }
	
  if(verbosity > 3)
    cerr << "TreePiece " << thisIndex << ": Writing my statistics to disk" << endl;
	
  FILE* outfile = fopen((basefilename + ".MACs").c_str(), "r+b");
  fseek(outfile, 0, SEEK_END);
  XDR xdrs;
  xdrstdio_create(&xdrs, outfile, XDR_ENCODE);
	
  for(unsigned int i = 1; i <= myNumParticles; ++i) {
    macInterval.grow(myParticles[i].numMACChecks);
    if(!xdr_template(&xdrs, &(myParticles[i].numMACChecks))) {
      cerr << "TreePiece " << thisIndex << ": Error writing MAC checks to disk, aborting" << endl;
      CkAbort("Badness");
    }
  }
	
  if(thisIndex == (int) numTreePieces - 1) {
    if(verbosity > 3)
      cerr << "MAC interval: " << macInterval << endl;
    if(!xdr_setpos(&xdrs, FieldHeader::sizeBytes) || !xdr_template(&xdrs, &macInterval.min) || !xdr_template(&xdrs, &macInterval.max)) {
      cerr << "TreePiece " << thisIndex << ": Error going back to write the MAC bounds, aborting" << endl;
      CkAbort("Badness");
    }
    if(verbosity > 2)
      cerr << "TreePiece " << thisIndex << ": Wrote the MAC bounds" << endl;
  }
	
  xdr_destroy(&xdrs);
  fclose(outfile);
	
  outfile = fopen((basefilename + ".cellints").c_str(), "r+b");
  fseek(outfile, 0, SEEK_END);
  xdrstdio_create(&xdrs, outfile, XDR_ENCODE);	
  for(unsigned int i = 1; i <= myNumParticles; ++i) {
    cellInterval.grow(myParticles[i].numCellInteractions);
    if(!xdr_template(&xdrs, &(myParticles[i].numCellInteractions))) {
      cerr << "TreePiece " << thisIndex << ": Error writing cell interactions to disk, aborting" << endl;
      CkAbort("Badness");
    }
  }
  if(thisIndex == (int) numTreePieces - 1) {
    if(verbosity > 3)
      cerr << "Cell interactions interval: " << cellInterval << endl;
    if(!xdr_setpos(&xdrs, FieldHeader::sizeBytes) || !xdr_template(&xdrs, &cellInterval.min) || !xdr_template(&xdrs, &cellInterval.max)) {
      cerr << "TreePiece " << thisIndex << ": Error going back to write the cell interaction bounds, aborting" << endl;
      CkAbort("Badness");
    }
    if(verbosity > 2)
      cerr << "TreePiece " << thisIndex << ": Wrote the cell interaction bounds" << endl;
  }
  xdr_destroy(&xdrs);
  fclose(outfile);

  outfile = fopen((basefilename + ".calls").c_str(), "r+b");
  fseek(outfile, 0, SEEK_END);
  xdrstdio_create(&xdrs, outfile, XDR_ENCODE);	
  for(unsigned int i = 1; i <= myNumParticles; ++i) {
    callsInterval.grow(myParticles[i].numEntryCalls);
    if(!xdr_template(&xdrs, &(myParticles[i].numEntryCalls))) {
      cerr << "TreePiece " << thisIndex << ": Error writing entry calls to disk, aborting" << endl;
      CkAbort("Badness");
    }
  }
  if(thisIndex == (int) numTreePieces - 1) {
    if(verbosity > 3)
      cerr << "Entry call interval: " << callsInterval << endl;
    if(!xdr_setpos(&xdrs, FieldHeader::sizeBytes) || !xdr_template(&xdrs, &callsInterval.min) || !xdr_template(&xdrs, &callsInterval.max)) {
      cerr << "TreePiece " << thisIndex << ": Error going back to write the entry call bounds, aborting" << endl;
      CkAbort("Badness");
    }
    if(verbosity > 2)
      cerr << "TreePiece " << thisIndex << ": Wrote the entry call bounds" << endl;
  }
  xdr_destroy(&xdrs);
  fclose(outfile);

  outfile = fopen((basefilename + ".partints").c_str(), "r+b");
  fseek(outfile, 0, SEEK_END);
  xdrstdio_create(&xdrs, outfile, XDR_ENCODE);	
  for(unsigned int i = 1; i <= myNumParticles; ++i) {
    particleInterval.grow(myParticles[i].numParticleInteractions);
    if(!xdr_template(&xdrs, &(myParticles[i].numParticleInteractions))) {
      cerr << "TreePiece " << thisIndex << ": Error writing particle interactions to disk, aborting" << endl;
      CkAbort("Badness");
    }
  }
  if(thisIndex == (int) numTreePieces - 1) {
    if(verbosity > 3)
      cerr << "Particle interactions interval: " << particleInterval << endl;
    if(!xdr_setpos(&xdrs, FieldHeader::sizeBytes) || !xdr_template(&xdrs, &particleInterval.min) || !xdr_template(&xdrs, &particleInterval.max)) {
      cerr << "TreePiece " << thisIndex << ": Error going back to write the particle interaction bounds, aborting" << endl;
      CkAbort("Badness");
    }
    if(verbosity > 2)
      cerr << "TreePiece " << thisIndex << ": Wrote the particle interaction bounds" << endl;
    cb.send();
  }		
  xdr_destroy(&xdrs);
  fclose(outfile);
	
  if(thisIndex != (int) numTreePieces - 1)
    pieces[thisIndex + 1].outputStatistics(macInterval, cellInterval, particleInterval, callsInterval, totalmass, cb);
}

void TreePiece::outputRelativeErrors(Interval<double> errorInterval, const CkCallback& cb) {
  if(thisIndex == 0) {
    if(verbosity > 2)
      cerr << "TreePiece " << thisIndex << ": Writing header for errors file" << endl;
    FILE* outfile = fopen((basefilename + ".error").c_str(), "wb");
    XDR xdrs;
    xdrstdio_create(&xdrs, outfile, XDR_ENCODE);
    fh.code = float64;
    fh.dimensions = 1;
    if(!xdr_template(&xdrs, &fh) || !xdr_template(&xdrs, &errorInterval.min) || !xdr_template(&xdrs, &errorInterval.max)) {
      cerr << "TreePiece " << thisIndex << ": Could not write header to errors file, aborting" << endl;
      CkAbort("Badness");
    }
    xdr_destroy(&xdrs);
    fclose(outfile);
  }
	
  if(verbosity > 3)
    cerr << "TreePiece " << thisIndex << ": Writing my errors to disk" << endl;
	
  FILE* outfile = fopen((basefilename + ".error").c_str(), "r+b");
  fseek(outfile, 0, SEEK_END);
  XDR xdrs;
  xdrstdio_create(&xdrs, outfile, XDR_ENCODE);
	
  double error;
	
  for(unsigned int i = 1; i <= myNumParticles; ++i) {
    error = (myParticles[i].treeAcceleration - myParticles[i].acceleration).length() / myParticles[i].acceleration.length();
    errorInterval.grow(error);
    if(!xdr_template(&xdrs, &error)) {
      cerr << "TreePiece " << thisIndex << ": Error writing errors to disk, aborting" << endl;
      CkAbort("Badness");
    }
  }
	
  if(thisIndex == (int) numTreePieces - 1) {
    if(!xdr_setpos(&xdrs, FieldHeader::sizeBytes) || !xdr_template(&xdrs, &errorInterval.min) || !xdr_template(&xdrs, &errorInterval.max)) {
      cerr << "TreePiece " << thisIndex << ": Error going back to write the error bounds, aborting" << endl;
      CkAbort("Badness");
    }
    if(verbosity > 2)
      cerr << "TreePiece " << thisIndex << ": Wrote the error bounds" << endl;
    cerr << "Error Bounds:" << errorInterval.min << ", "
	 << errorInterval.max << endl;
    cb.send();
  }
	
  xdr_destroy(&xdrs);
  fclose(outfile);
	
  if(thisIndex != (int) numTreePieces - 1)
    pieces[thisIndex + 1].outputRelativeErrors(errorInterval, cb);
}

void TreePiece::pup(PUP::er& p) {
  ckout << "TreePiece " << thisIndex << ": Getting PUP'd!" << endl;
  CBase_TreePiece::pup(p);
  p | numTreePieces;
  p | callback;
  p | myNumParticles;
  if(p.isUnpacking()) {
    myParticles = new GravityParticle[myNumParticles + 2];
    leftBoundary = myParticles;
    rightBoundary = myParticles + myNumParticles + 1;
  }
  for(int i=0;i<myNumParticles+2;i++){
    p |myParticles[i];
  }
  p | numSplitters;
  if(p.isUnpacking())
    splitters = new Key[numSplitters];
  p(splitters, numSplitters);
  p | pieces;
  p | streamingProxy;
  p | basefilename;
  p | boundingBox;
  p | fh;
  p | started;
  p | iterationNo;
  if(p.isUnpacking()){
    root = new SFCTreeNode;
  }
  p | (*root);
  if(p.isUnpacking()){
    nodeLookup[root->lookupKey()]=root;
  }
  p | boundaryNodesPending;
  //p | tempNode;
  p | theta;
  p | mySerialNumber;
  p | myNumParticlesPending;
  p | numBuckets;
  p | currentBucket;
	/***************/
	p | myNumParticleInteractions;
	p | myNumCellInteractions;
	p | myNumMACChecks;
	p | piecemass;
	/**************/
  if(p.isUnpacking()){
    localCache=cacheManagerProxy.ckLocalBranch();
  }
  if(!(p.isUnpacking())) {
	
    //Pack nodeLookup here
    int num=0;
    for (NodeLookupType::iterator iter=nodeLookup.begin();iter!=nodeLookup.end();iter++){
      if(iter->second != root && iter->second != NULL){
	num++;
      }	
    }
    p(num);
    for (NodeLookupType::iterator iter=nodeLookup.begin();iter!=nodeLookup.end();iter++){
      if(iter->second != root && iter->second != NULL){
	Key k = iter->first;
	p | k;
	p | (*(iter->second));
      }	
    }
  }else{
    int num;
    p(num);
    for(int i=0;i<num;i++){
      Key k;
      SFCTreeNode *n = new SFCTreeNode;
      p | k;
      p | *n;
      nodeLookup[k] = n;
      if(n->getType() == Bucket){
	bucketList.push_back(n);
      }
    }
    int count=0;
    rebuildSFCTree(root,NULL,&count);
    sort(bucketList.begin(),bucketList.end(),compBucket);
    if(verbosity)
			CkPrintf("[%d] TreePiece %d bucketList size %d numBuckets %d nodelookupsize %d count %d\n",CkMyPe(),thisIndex,bucketList.size(),numBuckets,num,count);
  }
  //p | unfilledRequests;
}

void TreePiece::rebuildSFCTree(SFCTreeNode *node,SFCTreeNode *parent,int *count){
  if(node == NULL){
    return;
  }
  (*count)++;
  node->parent = (GenericTreeNode *)parent;
  SFCTreeNode *lchild = nodeLookup[node->leftChildLookupKey()];
  node->leftChild = lchild;
  SFCTreeNode *rchild = nodeLookup[node->rightChildLookupKey()];
  node->rightChild = rchild;
  rebuildSFCTree(lchild,node,count);
  rebuildSFCTree(rchild,node,count);
}
bool compBucket(SFCTreeNode *ln,SFCTreeNode *rn){
  return (ln->beginParticle < rn->beginParticle);
}


/** Check that all the particles in the tree are really in their boxes.
    Because the keys are made of only the first 21 out of 23 bits of the
    floating point representation, there can be particles that are outside
    their box by tiny amounts.  Whether this is bad is not yet known. */
void TreePiece::checkTree(SFCTreeNode* node) {
  if(node->getType() == Bucket) {
    for(unsigned int iter = node->beginParticle; iter != node->endParticle; ++iter) {
      if(!node->boundingBox.contains(myParticles[iter].position))
	cerr << "Not in the box: Box: " << node->boundingBox << " Position: " << myParticles[iter].position << "\nNode key: " << keyBits(node->key, node->level) << "\nParticle key: " << keyBits(myParticles[iter].key, 63) << endl;
    }
  } else if(node->getType() != NonLocal) {
    GenericTreeNode** childrenIterator = node->getChildren();
    for(unsigned int i = 0; i < node->numChildren(); ++i) {
      if(childrenIterator[i])
	checkTree(dynamic_cast<SFCTreeNode *>(childrenIterator[i]));
    }
  }
}

/// Color a node
string getColor(SFCTreeNode* node) {
  ostringstream oss;
  switch(node->getType()) {
  case Bucket:
  case Internal:
    oss << "black";
    break;
  case NonLocal:
    oss << "red";
    break;
  case Boundary:
    oss << "purple";
    break;
  default:
    oss << "yellow";
  }
  return oss.str();
}

/// Make a label for a node
string makeLabel(SFCTreeNode* node) {
  ostringstream oss;
  oss << keyBits(node->key, node->level) << "\\n";
  switch(node->getType()) {
  case Invalid:
    oss << "Invalid";
    break;
  case Bucket:
    //oss << "Bucket: " << (node->endParticle - node->beginParticle) << " particles";
    oss << "Bucket";
    break;
  case Internal:
    oss << "Internal";
    break;
  case NonLocal:
    oss << "NonLocal: Chare " << node->remoteIndex;
    break;
  case Empty:
    oss << "Empty";
    break;
  case Boundary:
    oss << "Boundary: Total N " << node->remoteIndex;
    break;
  case Top:
    oss << "Top";
    break;
  default:
    oss << "Unknown NodeType!";
  }
  return oss.str();
}

/// Print a graphviz version of a tree
void printTree(SFCTreeNode* node, ostream& os) {
  if(node == 0)
    return;
	
  string nodeID = keyBits(node->key, node->level);
  os << "\tnode [color=\"" << getColor(node) << "\"]\n";
  //os << "\t\"" << nodeID << "\" [label=\"" << makeLabel(node) << "\\nCM: " << (node->moments.cm) << "\\nM: " << node->moments.totalMass << "\\nN_p: " << (node->endParticle - node->beginParticle) << "\\nOwners: " << node->numOwners << "\"]\n";
  //os << "\t\"" << nodeID << "\" [label=\"" << makeLabel(node) << "\\nLocal N: " << (node->endParticle - node->beginParticle) << "\\nOwners: " << node->numOwners << "\"]\n";
  os << "\t\"" << nodeID << "\" [label=\"" << keyBits(node->key, node->level) << "\\n";
  switch(node->getType()) {
  case Bucket:
    os << "Bucket\\nSize: " << (node->endParticle - node->beginParticle);
    break;
  case Internal:
    os << "Internal\\nLocal N under: " << (node->endParticle - node->beginParticle);
    break;
  case NonLocal:
    os << "NonLocal: Chare " << node->remoteIndex << "\\nRemote N under: " << (node->endParticle - node->beginParticle) << "\\nOwners: " << node->numOwners;
    break;
  case Boundary:
    os << "Boundary\\nTotal N under: " << node->remoteIndex << "\\nLocal N under: " << (node->endParticle - node->beginParticle) << "\\nOwners: " << node->numOwners;
    break;
  }
  os << "\"]\n";
	
  if(node->parent)
    os << "\t\"" << keyBits(node->key, node->level - 1) << "\" -> \"" << nodeID << "\";\n";
	
  if(node->getType() == NonLocal || node->getType() == Bucket)
    return;

  GenericTreeNode** childrenIterator = node->getChildren();
  for(unsigned int i = 0; i < node->numChildren(); ++i) {
    if(childrenIterator[i])
      printTree(dynamic_cast<SFCTreeNode *>(childrenIterator[i]), os);
    else {
      os << "\tnode [color=\"green\"]\n";
      os << "\t\"" << nodeID << i << "\" [label=\"None\"]\n";
      os << "\t\"" << nodeID << "\" -> \"" << nodeID << i << "\";\n";
    }
  }
}

/// Write a file containing a graphviz dot graph of my tree
void TreePiece::report(const CkCallback& cb) {
  ostringstream outfilename;
  outfilename << "tree_" << thisIndex << ".dot";
  ofstream os(outfilename.str().c_str());

  os << "digraph G" << thisIndex << " {\n";
  os << "\tcenter = \"true\"\n";
  os << "\tsize = \"7.5,10\"\n";
  //os << "\tratio = \"fill\"\n";
  //os << "\tfontname = \"Courier\"\n";
  os << "\tnode [style=\"bold\"]\n";
  os << "\tlabel = \"Piece: " << thisIndex << "\\nParticles: " 
     << myNumParticles << "\"\n";
  /*	os << "\tlabel = \"Piece: " << thisIndex << "\\nParticles: " 
	<< myNumParticles << "\\nLeft Splitter: " << keyBits(myParticles[0].key, 63)
	<< "\\nLeftmost Key: " << keyBits(myParticles[1].key, 63) 
	<< "\\nRightmost Key: " << keyBits(myParticles[myNumParticles].key, 63) 
	<< "\\nRight Splitter: " << keyBits(myParticles[myNumParticles + 1].key, 63) << "\";\n";
  */
  os << "\tfontname = \"Helvetica\"\n";
  printTree(root, os);
  os << "}" << endl;
	
  os.close();
	
  //checkTree(root);
		
  contribute(0, 0, CkReduction::concat, cb);
}

/*********************ADDED***********************/
void TreePiece::getPieceValues(piecedata *totaldata){

  totaldata->modifypiecedata(myNumCellInteractions,myNumParticleInteractions,myNumMACChecks,piecemass);
  if(thisIndex != (int) numTreePieces - 1)
  	pieces[thisIndex + 1].getPieceValues(totaldata);
  else {
    CkCallback& cb= totaldata->getcallback();
    cb.send(totaldata);
  }
}
/*************************************************/
