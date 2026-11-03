/* SPDX-License-Identifier: MIT. Journaled file transaction core. */
#pragma once
#include <memory>
#include <string>
#include <vector>
namespace transaction {
struct Store {
  virtual ~Store()=default;
  virtual bool exists(const std::string&)=0;
  virtual bool read(const std::string&,std::string&)=0;
  virtual bool writeNew(const std::string&,const std::string&)=0;
  virtual bool replace(const std::string&,const std::string&)=0;
  virtual bool erase(const std::string&)=0;
};
struct Change { std::string target,expectedBefore;std::shared_ptr<const std::string> data; };
struct Context { std::string identity;bool sessionIdentityVerified=false; };
struct Result { bool ok;std::string message;Result(bool value=false,const std::string& text=""):ok(value),message(text){} };
std::string digest(const std::string&);
Result install(Store&,const std::string& id,const std::vector<Change>&,const Context&);
Result restore(Store&,const std::string& id,const Context&);
}
