#include "nbdserv.h"

#include <fstream>
#include <string>
#include <sstream>

// modeled on DevExample from nbdserv.h
// uses a file (or device file) for backend storage
template <unsigned BS=512>
class BSwap16 {
  private:
    mutable std::fstream _file;
    int _nblocks;

    static constexpr std::ios_base::openmode openflags() {
      return std::fstream::in | std::fstream::out | std::fstream::binary;
    }

  public:
    BSwap16(const std::string& fname, size_t nblocks=0)
      :_file(fname.c_str(), openflags())
    {
      decltype(_file.tellg()-_file.tellg()) actsize;
      if (_file) {
        // check the file size
        auto start = _file.tellg();
        _file.seekg(0, std::ios::end);
        actsize = _file.tellg() - start;
      } else {
        if (nblocks == 0) {
          nbdcpp::errout() << "Error: file " << fname << " doesn't exist or couldn't be opened\n";
          exit(1);
        }
        // try opening with trunc to create it
        _file.open(fname.c_str(), openflags() | std::fstream::trunc);
        if (!_file) {
          nbdcpp::errout() << "ERROR: could not open or create file " << fname << "\n";
          exit(1);
        }
        actsize = 0;
      }

      if (nblocks == 0) {
        // determine nblocks from actual size
        if (actsize <= 0) {
          nbdcpp::errout() << "Error: file is empty and you did not specify a size\n";
          exit(1);
        }
        _nblocks = actsize / blocksize();
      } else {
        // make sure file is large enough
        decltype(actsize) totsize = nblocks * blocksize();
        if (actsize < totsize) {
          nbdcpp::logout() << "Warning: increasing file size from "
            << actsize << " to " << totsize << " bytes"
            << std::endl;
          _file.seekp(totsize - 1);
          _file.put('\0');
        }
        _nblocks = nblocks;
      }
    }

    // don't allow copying
    BSwap16(const BSwap16&) = delete;
    BSwap16& operator=(const BSwap16&) = delete;

    // should return false if some unrecoverable error has occurred
    bool good() const { return _file.good(); }

    // number of bytes per block
    static constexpr size_t blocksize() { return BS; }

    // number of blocks in device
    size_t numblocks() const { return _nblocks; }

    // read a single block from the device
    // index is the index of the block
    // data is a pointer to an array of size at last blocksize()
    void read(size_t index, nbdcpp::byte* data) const {
      
      char *d = reinterpret_cast<char*>(data);
      
      _file.seekg(index * blocksize());
      _file.read(d, blocksize());
      
      for (size_t i = 0; i < blocksize(); i+=2)
      {
      	char b = d[i];
      	d[i] = d[i+1];
      	d[i+1] = b;
      }
    }

    // write a single block to the device
    // index is the index of the block
    // data is a pointer to an array of size at last blocksize()
    void write(size_t index, const nbdcpp::byte* data) {
      
      char *d = new char[blocksize()];
      memcpy (d, reinterpret_cast<const char*>(data), blocksize());
      
      for (size_t i = 0; i < blocksize(); i+=2)
      {
      	char b = d[i];
      	d[i] = d[i+1];
      	d[i+1] = b;
      }
      
      _file.seekp(index * blocksize());
      _file.write(d, blocksize());
      
      delete[] d;
    }

    // read multiple blocks at once
    void multiread(size_t index, size_t count, nbdcpp::byte* data) const {
    
      char *d = reinterpret_cast<char*>(data);
    
      _file.seekg(index * blocksize());
      _file.read(d, count * blocksize());
      
      for (size_t i = 0; i < count * blocksize(); i+=2)
      {
      	char b = d[i];
      	d[i] = d[i+1];
      	d[i+1] = b;
      }
    }

    // write multiple blocks at once
    void multiwrite(size_t index, size_t count, const nbdcpp::byte* data) {

      char *d = new char[count*blocksize()];
      memcpy (d, reinterpret_cast<const char*>(data), count*blocksize());
      
      for (size_t i = 0; i < count * blocksize(); i+=2)
      {
      	char b = d[i];
      	d[i] = d[i+1];
      	d[i+1] = b;
      }
      
      _file.seekp(index * blocksize());
      _file.write(d, count * blocksize());
      
      delete[] d;
    }

    // returns true iff the flush operation is supported
    constexpr bool flushes() const { return true; }

    // Syncs all pending read/write ops to any underlying device
    void flush() const { _file.flush(); }

    // returns true iff the trim operation is supported
    constexpr bool trims() const { return false; }

    // Performs a DISCARD/TRIM operation (optional)
    void trim(size_t, size_t) { }
};

using namespace std;
using namespace nbdcpp;

int main(int argc, char** argv) {
  auto usage = [argv]() {
    errout() << "usage: " << argv[0] << " file [-s size]" << nbd_usage_line() << "\n";
    errout() << "  Provides a device with bytes swapped every 16 bits word from the given file.\n";
    errout() << "  size is in KB; if not given, the current filesize is used.\n";
    nbd_usage_doc(errout());
  };

  int argind = 1;

  // filename must be first command line argument
  if (argind >= argc) {
    usage();
    return 1;
  }
  string fname = argv[argind];
  if (fname.size() == 0 || fname[0] == '-') {
    usage();
    return 1;
  }
  ++argind;

  // optional size follows
  size_t size = 0;
  if (argind < argc && string(argv[argind]) == "-s") {
    ++argind;
    if (argind >= argc || !(istringstream(argv[argind]) >> size) || size <= 0) {
      usage();
      return 1;
    }
    ++argind;
    // convert from KB to number of blocks
    size = 1 + (size*1024 - 1) / BSwap16<>::blocksize();
  }

  // everything else is taken care of by nbdcpp
  return nbdcpp_main<BSwap16<>>(argc, argv, argind, usage, fname, size);
}
