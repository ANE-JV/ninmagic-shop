// SPDX-License-Identifier: GPL-3.0-or-later
#include "utils/ownfoil_profile.hpp"
#include <minIni.h>
#include <cassert>
#include <iostream>
#include <string>

using namespace sphaira::ownfoil;
int main(int argc, char** argv) {
    assert(argc == 2);
    const std::string filename = argv[1];
    assert(filename.ends_with("profiles.ini"));
    Config first;
    assert(first.direct_catalog.empty());
    assert(profile::ValidDirectCatalogName("catalogo-1_Juegos.JSON"));
    assert(profile::ValidDirectCatalogName(std::string(123, 'a') + ".json"));
    for (const auto* invalid : {"", ".json", ".hidden.json", "a..b.json", "../test.json",
            "folder/test.json", "folder\\test.json", "sdmc:test.json", "https://example.invalid/list.json",
            "bad?.json", "file%2f.json", "foo.txt", "a b.json", "a.json\n"})
        assert(!profile::ValidDirectCatalogName(invalid));
    assert(!profile::ValidDirectCatalogName(std::string(124, 'a') + ".json"));
    assert(!profile::ValidDirectCatalogName(std::string("a\0b.json", 8)));
    first.uid="pending-fixture-a";first.server_uid="old-server";first.name="Tienda A";
    first.local_address="https://old.invalid:8443/library/";first.remote_address="https://alternative.invalid/";
    first.user="fixture-user";first.pass="fixture-password";first.resolved_url=first.remote_address;
    first.legacy_html=true;first.json_index=true;first.favorites={"package-example"};
    Config second=first;second.uid="pending-fixture-b";second.name="Otra cuenta";second.user="second-user";second.pass="second-password";
    auto write=[&](const std::string& uid,const std::string& key,const std::string& value){return ini_puts(uid.c_str(),key.c_str(),value.c_str(),filename.c_str())!=0;};
    auto read=[&](const std::string& uid,const std::string& key){char value[1024]{};ini_gets(uid.c_str(),key.c_str(),"",value,sizeof(value),filename.c_str());return std::string(value);};
    assert(profile::WriteFields(first,write)&&profile::WriteFields(second,write));
    assert(ini_puts(first.uid.c_str(),"favorite_package-example","1",filename.c_str()));
    assert(ini_puts(second.uid.c_str(),"custom_preference","keep",filename.c_str()));
    const profile::Edit changes{.name="Tienda editada",.local_address="https://new.invalid:9090/new-route/",.remote_address=first.remote_address,
        .user="changed-user",.metadata_url=first.metadata_url,.artwork_search_url=first.artwork_search_url,.backend=Backend::JsonIndex,.password={}};
    const auto edited=profile::Apply(first,changes);
    assert(edited.uid==first.uid&&edited.favorites==first.favorites&&edited.pass==first.pass);
    assert(edited.resolved_url.empty()&&edited.server_uid.empty()&&!edited.legacy_html&&!edited.json_index&&edited.revalidate_artwork);
    assert(profile::WriteFields(edited,write));
    assert(read(first.uid,"local_address")==changes.local_address&&read(first.uid,"user")==changes.user&&read(first.uid,"pass")==first.pass);
    assert(read(first.uid,"resolved_url").empty()&&read(first.uid,"server_uid").empty());
    assert(read(first.uid,"favorite_package-example")=="1");
    assert(read(second.uid,"user")==second.user&&read(second.uid,"pass")==second.pass&&read(second.uid,"custom_preference")=="keep");
    char section[128]{};assert(ini_getsection(0,section,sizeof(section),filename.c_str()));assert(std::string(section)==first.uid);
    assert(ini_getsection(1,section,sizeof(section),filename.c_str()));assert(std::string(section)==second.uid);
    assert(!ini_getsection(2,section,sizeof(section),filename.c_str()));
    auto password_change=changes;password_change.password="new-fixture-password";
    const auto new_password=profile::Apply(first,password_change);assert(new_password.pass==*password_change.password&&new_password.resolved_url.empty());
    password_change.password="";assert(profile::Apply(first,password_change).pass.empty());
    auto name_only=changes;name_only.name="Solo nombre";name_only.local_address=first.local_address;name_only.remote_address=first.remote_address;
    name_only.user=first.user;name_only.backend=first.backend;
    const auto renamed=profile::Apply(first,name_only);assert(renamed.server_uid==first.server_uid&&renamed.resolved_url==first.resolved_url&&renamed.legacy_html&&renamed.json_index);
    auto local_catalog = name_only;
    local_catalog.direct_catalog = "biblioteca.json";
    const auto with_catalog = profile::Apply(first, local_catalog);
    assert(with_catalog.direct_catalog == "biblioteca.json" && with_catalog.uid == first.uid);
    assert(with_catalog.server_uid.empty() && with_catalog.resolved_url.empty() && with_catalog.revalidate_artwork);
    assert(!with_catalog.legacy_html && !with_catalog.json_index && with_catalog.pass == first.pass && with_catalog.favorites == first.favorites);
    assert(profile::WriteFields(with_catalog, write));
    assert(read(first.uid, "direct_catalog") == "biblioteca.json" && read(second.uid, "direct_catalog").empty());
    const auto cleared = profile::Apply(with_catalog, name_only);
    assert(cleared.direct_catalog.empty() && profile::WriteFields(cleared, write) && read(first.uid, "direct_catalog").empty());
    auto bad_catalog = with_catalog;
    bad_catalog.direct_catalog = "../otra-tienda.json";
    assert(!profile::WriteFields(bad_catalog, [](auto&, auto&, auto&) { assert(false); return true; }));
    auto credentials_only=name_only;credentials_only.user="wrong-fixture-user";
    const auto failed_auth=profile::Apply(first,credentials_only);
    assert(failed_auth.uid==first.uid&&failed_auth.resolved_url.empty()&&failed_auth.server_uid.empty()&&!failed_auth.legacy_html&&!failed_auth.json_index);
    assert(profile::WriteFields(failed_auth,write)&&read(first.uid,"user")==credentials_only.user);
    assert(read(second.uid,"user")==second.user&&read(second.uid,"custom_preference")=="keep");
    unsigned writes{};assert(!profile::WriteFields(first,[&](auto&,auto&,auto&){return ++writes!=3;}));assert(writes==3);
    std::cout<<"Ownfoil profile editing: same UID, URL/port/path, account changes, password retention, invalidation and independent INI sections PASS\n";
}
