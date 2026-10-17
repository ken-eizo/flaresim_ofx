#include "lens.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
int main(){
 const auto root=std::filesystem::path(FLARESIM_SOURCE_ROOT)/"resources";
 std::ifstream manifest(root/"LensCatalog.txt");
 if(!manifest){std::cerr<<"Missing manifest\n";return 1;}
 int count=0,errors=0,degenerate=0;std::set<std::string> names;std::string name;
 while(std::getline(manifest,name)){
  if(!name.empty()&&name.back()=='\r')name.pop_back();
  if(name.empty())continue;
  if(!names.insert(name).second){std::cerr<<"Duplicate "<<name<<"\n";++errors;continue;}
  const auto file=root/"lenses"/std::filesystem::u8path(name);
  if(!std::filesystem::is_regular_file(file)){std::cerr<<"Missing "<<name<<"\n";++errors;continue;}
  LensSystem lens;
  // The upstream Nuke file parser uses a narrow path (codepage-sensitive on Windows).
  if(!lens.load(file.string().c_str())||lens.surfaces.empty()){
    std::cerr<<"Unreadable "<<name<<"\n";++errors;
  }
  if(lens.surfaces.size()<2)++degenerate;
  ++count;
 }
 std::cout<<"Catalog total="<<count<<" invalid="<<errors<<" one_surface="<<degenerate<<"\n";
 return count==1370&&errors==0&&degenerate==2?0:2;
}
