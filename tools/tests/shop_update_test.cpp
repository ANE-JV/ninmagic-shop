// SPDX-License-Identifier: GPL-3.0-or-later
#include "shop_update.hpp"
#include <algorithm>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <vector>
using namespace sphaira::shop_update;
namespace {
unsigned checks{}, scenarios{};
void Check(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
struct PowerCut {};
struct Node { Entry type{Entry::File}; std::string text; std::optional<Identity> binary; };
struct Disk : Storage {
    std::map<std::string, Node> files;
    std::vector<std::string> operations;
    size_t mutation{}, cut{}, fail{};
    enum class Timing { Before, After, Partial } timing{Timing::After};
    void Change(const std::string& label, const std::function<void()>& complete, const std::function<void()>& partial = {}) {
        operations.push_back(label); ++mutation;
        if (mutation == cut && timing == Timing::Before) throw PowerCut{};
        if (mutation == cut && timing == Timing::Partial && partial) { partial(); throw PowerCut{}; }
        complete();
        if (mutation == cut) throw PowerCut{};
    }
    bool Permit(std::string& error) { if (mutation + 1 == fail) { ++mutation; error = "simulated SD failure"; return false; } return true; }
    bool Probe(const std::string& path, Entry& entry, std::string&) override { const auto found=files.find(path); entry=found==files.end()?Entry::Missing:found->second.type; return true; }
    bool ReadText(const std::string& path, size_t max, std::string& text, std::string& error) override {
        const auto found=files.find(path); if(found==files.end()||found->second.type!=Entry::File||found->second.text.size()>max){error="invalid text";return false;}text=found->second.text;return true;
    }
    bool WriteNew(const std::string& path, std::string_view text, std::string& error) override {
        if(!Permit(error)||files.contains(path)){error="write collision";return false;}
        Change("write "+path,[&]{files[path]={Entry::File,std::string(text),{}};},[&]{files[path]={Entry::File,std::string(text.substr(0,text.size()/2)),{}};});return true;
    }
    bool CopyNew(const std::string& source, const std::string& destination, std::string& error) override {
        if(!Permit(error)||!files.contains(source)||files.contains(destination)){error="copy collision/missing source";return false;}
        Change("copy "+destination,[&]{files[destination]=files.at(source);},[&]{files[destination]={Entry::File,"partial bytes",{}};});return true;
    }
    bool Move(const std::string& source, const std::string& destination, std::string& error) override {
        if(!Permit(error)||!files.contains(source)||files.contains(destination)){error="rename collision/missing source";return false;}
        Change("rename "+source,[&]{files[destination]=files.at(source);files.erase(source);});return true;
    }
    bool Remove(const std::string& path, std::string& error) override {
        if(!Permit(error)||!files.contains(path)){error="remove missing";return false;}
        Change("remove "+path,[&]{files.erase(path);});return true;
    }
    bool Commit(std::string& error) override { if(!Permit(error))return false;Change("commit",[]{});return true; }
    bool Inspect(const std::string& path, Identity& identity, std::string& error) override {
        const auto found=files.find(path);if(found==files.end()||!found->second.binary||found->second.type!=Entry::File){error="invalid NRO identity";return false;}identity=*found->second.binary;return true;
    }
};
const Identity OLD{"0.1.0",std::string(64,'a'),30000000};
const Identity NEW{"0.2.0",std::string(64,'b'),31000000};
Manifest Release() { return {NEW.version,"https://raw.githubusercontent.com/ANE-JV/ninmagic-shop/0123456789abcdef0123456789abcdef01234567/releases/0.2.0/NinMagic-Shop.nro",NEW.sha256,"Mejoras de la tienda.",NEW.bytes}; }
std::string Json(const Manifest& release) { return "{\"schema\":1,\"app\":\"NinMagic Shop\",\"version\":\""+release.version+"\",\"download_url\":\""+release.download_url+"\",\"bytes\":"+std::to_string(release.bytes)+",\"sha256\":\""+release.sha256+"\",\"notes\":\""+release.notes+"\"}"; }
Disk Fresh() {
    Disk disk; disk.files[Target]={Entry::File,"original Shop",OLD};
    disk.files["/hbmenu.nro"]={Entry::File,"unrelated hbmenu",{}};
    disk.files["/switch/NinMagicGuardian/NinMagic-Guardian.nro"]={Entry::File,"unrelated Guardian",{}};
    disk.files["/Nintendo/Contents/user-content"]={Entry::File,"game data",{}};
    disk.files["/config/NinMagicShop/ownfoil.ini"]={Entry::File,"private profiles",{}};
    return disk;
}
Fetch DownloadTo(Disk& disk, const Identity& body=NEW) {
    return [&disk,body](const std::string& path,std::string& error){
        if(!disk.Permit(error))return false;
        disk.Change("download",[&]{disk.files[path]={Entry::File,"official download",body};},[&]{disk.files[path]={Entry::File,"incomplete transfer",{}};});return true;
    };
}
void Untouched(const Disk& disk) {
    Check(disk.files.at("/hbmenu.nro").text=="unrelated hbmenu","hbmenu changed");
    Check(disk.files.at("/switch/NinMagicGuardian/NinMagic-Guardian.nro").text=="unrelated Guardian","another app changed");
    Check(disk.files.at("/Nintendo/Contents/user-content").text=="game data","game content changed");
    Check(disk.files.at("/config/NinMagicShop/ownfoil.ini").text=="private profiles","account profiles changed");
}
void SafeTarget(Disk& disk) {
    Identity current;std::string error;
    Check(disk.Inspect(Target,current,error),"target not restored");
    Check(current==OLD||current==NEW,"target contains unexpected bytes");
    Untouched(disk);
}
void Retry(Disk& disk) {
    disk.cut=0;disk.fail=0;std::string error;
    Check(Recover(disk,error),error.c_str());
    Check(Recover(disk,error),"recovery is not idempotent");
    SafeTarget(disk);
    if(*disk.files.at(Target).binary==OLD) Check(Apply(disk,Release(),DownloadTo(disk),error),error.c_str());
    Check(*disk.files.at(Target).binary==NEW,"retry did not reach published release");
    Check(disk.files.at("/config/NinMagicShop/update/backup-"+OLD.sha256+".nro").binary==OLD,"immutable backup lost");
    Check(!disk.files.contains(Candidate)&&!disk.files.contains(Rollback)&&!disk.files.contains(Recovery)&&!disk.files.contains(Journal)&&!disk.files.contains(JournalPending),"active transaction remnants remain");
    Untouched(disk);
}
void Replace(std::string& value,const std::string& from,const std::string& to){auto at=value.find(from);Check(at!=std::string::npos,"test replacement missing");value.replace(at,from.size(),to);}
}
int main(){try{
    std::string error;Manifest parsed;
    ++scenarios;Check(ParseManifest(Json(Release()),parsed,error)&&parsed.bytes==NEW.bytes&&parsed.version==NEW.version,"valid schema rejected");
    for(const auto& mutation:std::vector<std::pair<std::string,std::string>>{{"\"schema\":1","\"schema\":2"},{"NinMagic Shop","sphaira"},{"\"bytes\":31000000","\"bytes\":-1"},{"\"bytes\":31000000","\"bytes\":67108865"},{"0.2.0","0.02.0"},{std::string(64,'b'),std::string(64,'G')},{"0123456789abcdef0123456789abcdef01234567","main"},{"raw.githubusercontent.com","evil.test"},{"https://","http://"}}){++scenarios;auto json=Json(Release());Replace(json,mutation.first,mutation.second);Check(!ParseManifest(json,parsed,error),"invalid manifest accepted");}
    ++scenarios;auto duplicate=Json(Release());duplicate.insert(1,"\"version\":\"99.0.0\",");Check(!ParseManifest(duplicate,parsed,error),"duplicate fields accepted");
    ++scenarios;Check(!ParseManifest(std::string(MaximumManifestBytes+1,' '),parsed,error),"unbounded manifest accepted");
    ++scenarios;Check(IsShopIdentity("NinMagic Shop","NinMagic","0.1.0"),"own identity rejected");
    for(const auto& name:{"sphaira","NinMagic Guardian","NinMagic Shop Extra","ninmagic shop"}){++scenarios;Check(!IsShopIdentity(name,"NinMagic","0.1.0"),"foreign identity accepted");}
    ++scenarios;Check(!IsShopIdentity("NinMagic Shop","Other","0.1.0")&&!IsShopIdentity("NinMagic Shop","NinMagic","latest"),"wrong author/version accepted");
    ++scenarios;Check(IsVersionNewer("0.1.9","0.2.0")&&!IsVersionNewer("0.2.0","0.2.0")&&!IsVersionNewer("1.0.0","0.2.0"),"version ordering broken");
    for(const auto& suffix:{"?token=secret","#fragment","/../NinMagic-Shop.nro"}){++scenarios;Check(!IsImmutableDownloadUrl(Release().download_url+suffix),"unsafe URL accepted");}
    auto baseline=Fresh();++scenarios;Check(Apply(baseline,Release(),DownloadTo(baseline),error),error.c_str());const auto mutations=baseline.mutation;SafeTarget(baseline);Retry(baseline);
    size_t power_cases{};
    for(const auto timing:{Disk::Timing::Before,Disk::Timing::After,Disk::Timing::Partial})for(size_t point=1;point<=mutations;++point){
        ++scenarios;++power_cases;auto disk=Fresh();disk.cut=point;disk.timing=timing;
        try{Apply(disk,Release(),DownloadTo(disk),error);}catch(const PowerCut&){}
        try{Retry(disk);}catch(const std::exception& failure){throw std::runtime_error("power cut "+std::to_string(point)+" timing "+std::to_string(static_cast<int>(timing))+": "+failure.what());}
    }
    for(size_t point=1;point<=mutations;++point){++scenarios;auto disk=Fresh();disk.fail=point;Apply(disk,Release(),DownloadTo(disk),error);Retry(disk);}
    ++scenarios;{auto disk=Fresh();Identity corrupt=NEW;corrupt.sha256=std::string(64,'c');Check(!Apply(disk,Release(),DownloadTo(disk,corrupt),error),"wrong SHA accepted");Check(disk.files.at(Target).binary==OLD,"bad download replaced target");Untouched(disk);}
    ++scenarios;{auto disk=Fresh();Identity corrupt=NEW;--corrupt.bytes;Check(!Apply(disk,Release(),DownloadTo(disk,corrupt),error),"wrong size accepted");Check(disk.files.at(Target).binary==OLD,"short download replaced target");}
    ++scenarios;{auto disk=Fresh();disk.files[Candidate]={Entry::Other,"directory",{}};Check(!Apply(disk,Release(),DownloadTo(disk),error),"directory scratch accepted");Check(disk.files.at(Candidate).type==Entry::Other&&disk.files.at(Target).binary==OLD,"directory scratch changed");}
    ++scenarios;{auto disk=Fresh();disk.files[Rollback]={Entry::File,"unknown scratch",{}};Check(!Recover(disk,error),"unowned scratch removed");Check(disk.files.at(Rollback).text=="unknown scratch","unowned scratch overwritten");}
    ++scenarios;{auto disk=Fresh();disk.files["/config/NinMagicShop/update/backup-"+OLD.sha256+".nro"]={Entry::File,"damaged existing backup",{}};Check(!Apply(disk,Release(),DownloadTo(disk),error),"damaged immutable backup accepted");Check(disk.files.at("/config/NinMagicShop/update/backup-"+OLD.sha256+".nro").text=="damaged existing backup","existing backup overwritten");}
    ++scenarios;{auto disk=Fresh();const auto backup="/config/NinMagicShop/update/backup-"+OLD.sha256+".nro";disk.files[backup]={Entry::File,"prior immutable backup",OLD};Check(Apply(disk,Release(),DownloadTo(disk),error),error.c_str());Check(disk.files.at(backup).text=="prior immutable backup","verified existing backup overwritten");}
    ++scenarios;{auto disk=Fresh();Check(!Apply(disk,Release(),[](const std::string&,std::string& failure){failure="cancelled";return false;},error),"cancelled download accepted");Check(disk.files.at(Target).binary==OLD&&!disk.files.contains(Journal)&&!disk.files.contains(Candidate),"cancelled download began replacement");}
    // Recovery itself may be interrupted. Produce the state between the two
    // target renames, remove rollback to exercise copying the immutable backup,
    // then cut every recovery mutation and retry twice.
    Disk interrupted=Fresh();size_t after_old_rename{};
    for(size_t i=0;i<baseline.operations.size();++i)if(baseline.operations[i]==std::string("rename ")+Target){after_old_rename=i+1;break;}
    Check(after_old_rename!=0,"rename checkpoint missing");interrupted.cut=after_old_rename;
    try{Apply(interrupted,Release(),DownloadTo(interrupted),error);}catch(const PowerCut&){}
    interrupted.cut=0;interrupted.files.erase(Rollback);
    ++scenarios;{auto disk=interrupted;const Identity foreign{"9.0.0",std::string(64,'c'),32000000};disk.files[Target]={Entry::File,"external Shop install",foreign};Check(!Recover(disk,error),"foreign current target overwritten by recovery");Check(disk.files.at(Target).binary==foreign&&disk.files.contains(Journal),"foreign target or journal changed");Untouched(disk);}
    auto recovery_baseline=interrupted;recovery_baseline.mutation=0;Check(Recover(recovery_baseline,error),error.c_str());
    const auto recovery_mutations=recovery_baseline.mutation;size_t recovery_cases{};
    for(const auto timing:{Disk::Timing::Before,Disk::Timing::After,Disk::Timing::Partial})for(size_t point=1;point<=recovery_mutations;++point){++scenarios;++recovery_cases;auto disk=interrupted;disk.mutation=0;disk.cut=point;disk.timing=timing;try{Recover(disk,error);}catch(const PowerCut&){}Retry(disk);}
    std::cout<<"NinMagic Shop updater: "<<scenarios<<" scenarios, "<<checks<<" assertions PASS; "<<power_cases<<" apply power cuts across "<<mutations<<" mutations, "<<recovery_cases<<" recovery power cuts across "<<recovery_mutations<<" mutations.\n";
    std::cout<<"Production policy/recovery code exercised with an injected filesystem boundary. Physical SD, TLS and console controls are not covered.\n";
    return 0;
}catch(const std::exception& failure){std::cerr<<"FAIL: "<<failure.what()<<'\n';return 1;}}
