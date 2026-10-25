#pragma once
// SK2.14 read-only coverage inventory. Presence of a C++ branch never proves
// DX9 equivalence. Human assessments are explicit, with drift checks against
// the *current* renderer and separate columns for evidence and validation.
#include "top/BlendAudit.h"
#include "top/LegacyEffectGeometry.h"
#include <functional>
#include <iomanip>
#include <numeric>
#include <regex>
#include <stdexcept>

namespace top { namespace fx_coverage {
namespace fs = std::filesystem;
namespace ba = blend_audit;

struct TableRow {
    std::string kind,key,name,assessment,expected,token,note;
};
struct SourceEvidence {
    std::vector<int> outside,inside;
};
struct Source {
    std::vector<std::string> lines;
    int loaderBegin=0,loaderEnd=0;
    bool valid=false;
    SourceEvidence find(const std::regex& regex) const {
        SourceEvidence result;
        for(std::size_t i=1;i<lines.size();++i){
            if(!std::regex_search(lines[i],regex))continue;
            (loaderBegin && int(i)>=loaderBegin && int(i)<=loaderEnd ? result.inside : result.outside).push_back(int(i));
        }
        return result;
    }
};
inline std::string trim(std::string v){
    const auto a=v.find_first_not_of(" \r\t\n"),b=v.find_last_not_of(" \r\t\n");
    return a==std::string::npos?"":v.substr(a,b-a+1);
}
inline std::string stripComment(const std::string& src){
    bool quoted=false; bool escaped=false;
    for(std::size_t i=0;i<src.size();++i){
        if(src[i]=='"'&&!escaped)quoted=!quoted;
        if(!quoted&&src[i]=='/'&&i+1<src.size()&&src[i+1]=='/')return src.substr(0,i);
        if(src[i]=='\\'&&!escaped)escaped=true; else escaped=false;
    }
    return src;
}
inline Source readSource(const fs::path& file){
    Source src; std::ifstream in(file);
    if(!in)return src;
    src.lines.push_back("");
    std::string line;
    while(std::getline(in,line))src.lines.push_back(stripComment(line));
    // Find the function body rather than assuming a fixed line number.
    for(std::size_t i=1;i<src.lines.size();++i){
        if(src.lines[i].find("bool loadParticles(")==std::string::npos)continue;
        int depth=0; bool opened=false;
        for(std::size_t j=i;j<src.lines.size();++j){
            for(char c:src.lines[j]){
                if(c=='{'){++depth;opened=true;}
                else if(c=='}'&&opened)--depth;
            }
            if(opened&&depth==0){src.loaderBegin=int(i);src.loaderEnd=int(j);break;}
        }
        break;
    }
    src.valid=!src.lines.empty() && src.loaderBegin!=0;
    return src;
}
inline SourceEvidence findType(const Source& src,int type){
    return src.find(std::regex("\\b(?:profile\\.type|s\\.type)\\s*==\\s*"+std::to_string(type)+"\\b"));
}
inline SourceEvidence findToken(const Source& src,const std::string& token){
    if(token.empty())return {};
    std::string escaped;
    for(char c:token){if(c=='.')escaped+="\\.";else escaped+=c;}
    return src.find(std::regex(escaped+"\\b"));
}
inline std::string lineList(const std::vector<int>& v){
    std::string out;
    for(std::size_t i=0;i<v.size()&&i<8;++i){if(i)out+=' ';out+=std::to_string(v[i]);}
    if(v.size()>8)out+=" ...";
    return out;
}
inline std::string baseName(const std::string& value){
    return ba::lower(fs::path(value).filename().string());
}
inline std::string normalizedBase(std::string value){
    value=baseName(value);
    auto dot=value.find('.');
    auto stem=value.substr(0,dot);
    if(!stem.empty()&&stem.size()<8&&stem.find_first_not_of("0123456789")==std::string::npos)
        value=std::string(8-stem.size(),'0')+value;
    return value;
}
inline std::vector<std::string> possibleNames(const std::string& raw){
    std::vector<std::string> names;
    const auto name=normalizedBase(raw);
    if(name.empty())return names;
    names.push_back(name);
    if(name.find('.')==std::string::npos){names.push_back(name+".eff");names.push_back(name+".par");}
    return names;
}
inline bool readTable(const fs::path& path,std::vector<TableRow>& rows,std::string& error){
    std::ifstream in(path);
    if(!in){error="coverage table missing: "+path.string();return false;}
    std::string line;
    if(!std::getline(in,line)||trim(line)!="kind,key,name,assessment,expected_branch,source_token,note"){
        error="bad coverage table header";return false;
    }
    std::set<std::string> unique;
    while(std::getline(in,line)){
        if(trim(line).empty())continue;
        auto f=ba::splitCsv(line);
        if(f.size()!=7||!(f[0]=="type"||f[0]=="feature")||f[1].empty()||!unique.insert(f[0]+":"+f[1]).second){
            error="malformed or duplicate coverage table row";return false;
        }
        rows.push_back({f[0],f[1],f[2],f[3],f[4],f[5],f[6]});
    }
    return true;
}
// Skip texture/model lookup rows from AUDITORIA.md: those files are not
// .par/.eff roots. Reporting them as missing effects would inflate failures.
inline bool loadEffectDeps(const fs::path& path,std::vector<ba::SkillDep>& out,
                           std::size_t& skipped){
    std::ifstream in(path);if(!in)return false;
    std::string line;if(!std::getline(in,line))return false;
    const auto h=ba::splitCsv(line);
    auto column=[&](const std::string& name)->int{
        for(std::size_t i=0;i<h.size();++i)if(h[i]==name)return int(i);
        return -1;
    };
    const int id=column("skill_id"),name=column("skill_name"),ch=column("channel"),
              ref=column("referenced_name"),kind=column("dependency_type");
    if(id<0||ref<0)return false;
    const std::set<std::string> accepted={"scene_par","par_status","eff","par"};
    while(std::getline(in,line)){
        auto f=ba::splitCsv(line);
        if(f.size()<=std::size_t(std::max(id,ref)))continue;
        if(kind>=0 && (f.size()<=std::size_t(kind) || !accepted.count(f[kind]))){++skipped;continue;}
        out.push_back({f[id],name>=0&&std::size_t(name)<f.size()?f[name]:"",
            ch>=0&&std::size_t(ch)<f.size()?f[ch]:"",ba::lower(f[ref])});
    }
    return true;
}
inline const std::vector<std::pair<std::string,std::function<bool(const par_audit::System&)>>>& features(){
    static const std::vector<std::pair<std::string,std::function<bool(const par_audit::System&)>>> v = {
        {"billboard",[](const auto& s){return s.billboard;}},
        {"model",[](const auto& s){return !s.model.empty() && s.model!="0";}},
        {"path",[](const auto& s){return s.hasPath;}},
        {"delay",[](const auto& s){return s.delay>0;}},
        {"play",[](const auto& s){return s.play>0;}},
        {"offset",[](const auto& s){return std::any_of(s.offset.begin(),s.offset.end(),[](float x){return x!=0;});}},
        {"step",[](const auto& s){return s.step>0;}},
        {"direction",[](const auto& s){return std::any_of(s.direction.begin(),s.direction.end(),[](float x){return x!=0;});}},
        {"acceleration",[](const auto& s){return std::any_of(s.acceleration.begin(),s.acceleration.end(),[](float x){return x!=0;});}},
        {"range",[](const auto& s){return std::any_of(s.range.begin(),s.range.end(),[](float x){return x!=0;});}},
        {"path_velocity",[](const auto& s){return s.hasPath && s.pathVelocity!=0;}},
        {"curves",[](const auto& s){return s.frames>1;}},
        {"model_range",[](const auto& s){return s.modelRange;}},
        {"shade_flag",[](const auto& s){return s.shade;}},
        {"hit_effect",[](const auto& s){return !s.hitEffect.empty();}},
        {"filters",[](const auto& s){return s.minFilter!=0||s.magFilter!=0;}}
    };
    return v;
}
struct Instance {std::string file,id;};
struct Item {
    std::string kind,key,name,assessment,note,evidence,validation="not_visually_verified";
    std::size_t count=0;
    std::set<std::string> files,skills;
};
struct Options {
    fs::path client="Client",output="build/fx-coverage";
    fs::path deps="sources/auditoria-historica/dependencies.csv";
    fs::path table="sources/auditoria-historica/fx_port_coverage.csv";
    fs::path source="sources/Engine/src/top/EffectSystem.cpp";
    fs::path directStrips; // optional inventory generated from DX9 controller tail
    bool strict=false;
};
inline std::string classification(const std::string& model){
    if(legacy_mesh::recognized(model))return "native_primitive";
    auto b=ba::lower(model);
    if(b.size()>4 && b.substr(b.size()-4)==".lgo")return "external_lgo";
    if(b.empty()||b=="0")return "empty";
    return "unrecognized_model";
}
inline int run(const Options& opt,std::ostream& log){
    if(!fs::is_directory(opt.client/"effect")){log<<"Missing Client/effect directory\n";return 2;}
    if(!ba::outsideClient(opt.client,opt.output)){log<<"Refusing to write inside Client\n";return 2;}
    std::vector<TableRow> rules;std::string error;
    if(!readTable(opt.table,rules,error)){log<<error<<'\n';return 2;}
    auto renderer=readSource(opt.source);
    if(!renderer.valid){log<<"Cannot inspect loadParticles in: "<<opt.source<<'\n';return 2;}
    if(!opt.directStrips.empty()&&!fs::is_regular_file(opt.directStrips)){
        log<<"Missing direct-strip inventory: "<<opt.directStrips<<'\n';return 2;
    }
    std::error_code ec;
    fs::create_directories(opt.output,ec);if(ec){log<<"Cannot create output: "<<ec.message()<<'\n';return 2;}
    std::map<std::string,Item> items;
    // basename => files (collisions retained and reported, not silently merged).
    std::map<std::string,std::set<std::string>> basenameFiles;
    std::map<std::string,std::set<std::string>> fileItems,fileLinks;
    std::vector<std::tuple<std::string,std::string,std::string>> failures;
    std::set<std::string> directStripFiles;
    std::size_t parFiles=0,effFiles=0,parSystems=0,effParts=0,drift=0;
    auto increment=[&](const std::string& id,const std::string& file){
        auto& it=items[id];it.key=id; ++it.count;it.files.insert(file);fileItems[file].insert(id);
    };
    std::vector<fs::path> paths;
    for(fs::recursive_directory_iterator it(opt.client/"effect",fs::directory_options::skip_permission_denied,ec),end;
        it!=end;it.increment(ec)){
        if(ec){ec.clear();continue;}
        if(!it->is_regular_file(ec))continue;
        auto ext=ba::lower(it->path().extension().string());
        if(ext==".par"||ext==".eff")paths.push_back(it->path());
    }
    std::sort(paths.begin(),paths.end());
    for(const auto& path:paths){
        const auto rel=fs::relative(path,opt.client,ec).generic_string();
        basenameFiles[normalizedBase(path.filename().string())].insert(rel);
        if(ba::lower(path.extension().string())==".par"){
            ++parFiles;
            auto result=par_audit::inspectFile(path.string());
            if(!result.ok){failures.emplace_back(rel,"par",result.error);continue;}
            for(const auto& s:result.document.systems){
                ++parSystems;increment("type:"+std::to_string(s.type),rel);
                for(const auto& feature:features())if(feature.second(s))increment("feature:"+feature.first,rel);
                for(const auto& model:possibleNames(s.model))if(model.size()>4&&model.substr(model.size()-4)==".eff")
                    fileLinks[rel].insert(model);
            }
        }else{
            ++effFiles; LegacyEffectSummary effect;
            if(!inspectLegacyEff(path.string(),effect)||!effect.structured){
                failures.emplace_back(rel,"eff","not_fully_decoded");continue;
            }
            for(const auto& s:effect.document.parts){
                ++effParts;
                const auto cl=classification(s.model);
                if(cl!="empty")increment("eff_model:"+s.model,rel);
            }
        }
    }
    // Direct CMPStrip trails are not par_audit::Document::systems. Importing a
    // separate, attributed inventory prevents the six known trails vanishing.
    std::size_t directStripRecords=0,missingStripRecords=0;
    if(!opt.directStrips.empty()){
        std::ifstream in(opt.directStrips);std::string line;
        std::getline(in,line);
        if(trim(line)!="file,index,max_length,dummy_a,dummy_b,life,step,texture,src_blend,dst_blend,implementation"){
            log<<"Bad direct strip inventory header\n";return 2;
        }
        while(std::getline(in,line)){
            if(trim(line).empty())continue;
            auto f=ba::splitCsv(line);if(f.size()<10){log<<"Invalid direct strip row\n";return 2;}
            const std::string filename=normalizedBase(f[0]);
            if(!basenameFiles.count(filename)){
                ++missingStripRecords;continue;
            }
            for(const auto& rel:basenameFiles[filename]){
                increment("direct_cmpstrip",rel);directStripFiles.insert(rel);
            }
            ++directStripRecords;
        }
    }
    std::vector<std::tuple<std::string,std::string,std::string>> drifts;
    std::set<std::string> covered;
    for(const auto& r:rules){
        const auto id=r.kind+":"+r.key;covered.insert(id);
        auto& item=items[id];item.key=id;item.kind=r.kind;item.name=r.name;
        item.assessment=r.assessment;item.note=r.note;
        auto ev=r.kind=="type"?findType(renderer,std::stoi(r.key)):findToken(renderer,r.token);
        // Loader mentioning a type is not a runtime branch.
        const bool branch=!ev.outside.empty();
        if(r.kind=="type"&&((r.expected=="yes")!=branch)){
            ++drift;drifts.emplace_back(id,"branch_expectation_mismatch",r.expected=="yes"?"missing_branch":"unexpected_branch");
        }
        if(r.kind=="feature"){
            if(ev.outside.empty())item.evidence=ev.inside.empty()?"not_observed":"parsed_only";
            else item.evidence=ev.inside.empty()?"referenced_outside_loader":"parsed_and_referenced";
        } else item.evidence=branch?"branch_present":"generic_or_no_branch";
        item.note += " | code_lines="+lineList(ev.outside)+" | loader_lines="+lineList(ev.inside);
    }
    for(auto& kv:items){
        auto& item=kv.second;
        if(!covered.count(kv.first)&&kv.first!="direct_cmpstrip" && kv.first.rfind("eff_model:",0)!=0){
            item.kind=kv.first.substr(0,kv.first.find(':'));item.name=kv.first;item.assessment="unknown";
            item.evidence="not_in_rules";item.note="Data found but no rule in table";
            ++drift;drifts.emplace_back(kv.first,"missing_rule","add review entry");
        }
        if(kv.first.rfind("eff_model:",0)==0){
            const auto model=kv.first.substr(10);
            item.kind="eff_model";item.name=model;item.assessment=classification(model);
            item.evidence="model_field_decoded";item.note="model name from .eff; geometry not run";
        }
    }
    if(items.count("direct_cmpstrip")){
        auto& item=items["direct_cmpstrip"];item.kind="controller_strip";item.name="CMPStrip (direct controller)";
        const bool present=!findToken(renderer,"legacy_direct_strip::emit").outside.empty();
        item.assessment=present?"partial_anchor_dependent":"not_rendered";
        item.evidence=present?"renderer_source_and_external_inventory":"external_inventory";
        item.note=present?"direct strips render from live paired actor/item dummies; non-player anchors and visual parity pending"
                         :"separate from normal type=6 systems; not decoded by par_audit";
    }
    // A manifest with a missing mandatory rule is itself a drift signal.
    for(int t=1;t<=18;++t)if(!covered.count("type:"+std::to_string(t))){
        ++drift;drifts.emplace_back("type:"+std::to_string(t),"missing_rule","required historical type");
    }
    // Link to skills: traverse direct resources + nested .par->.eff dependencies.
    std::map<std::string,std::string> skillNames;
    std::size_t dependencyRows=0,unmatched=0,skippedDeps=0;
    std::set<std::pair<std::string,std::string>> missingLinks;
    std::vector<ba::SkillDep> deps;
    const bool haveDeps=!opt.deps.empty()&&loadEffectDeps(opt.deps,deps,skippedDeps);
    if(!opt.deps.empty()&&!haveDeps){log<<"WARNING: dependencies not available; skill counts are unknown\n";}
    if(haveDeps){
        for(const auto& d:deps){
            ++dependencyRows; skillNames[d.id]=d.name;
            std::vector<std::string> frontier=possibleNames(d.file);
            std::set<std::string> visited;
            bool found=false;
            for(std::size_t pos=0;pos<frontier.size()&&pos<2000;++pos){
                const auto n=frontier[pos];
                auto fit=basenameFiles.find(n);
                if(fit==basenameFiles.end())continue;
                for(const auto& path:fit->second){
                    if(!visited.insert(path).second)continue;
                    found=true;
                    for(const auto& id:fileItems[path])items[id].skills.insert(d.id);
                    for(const auto& child:fileLinks[path]){
                        for(const auto& target:possibleNames(child))frontier.push_back(target);
                    }
                }
            }
            if(!found){++unmatched;missingLinks.insert({d.id,d.file});}
        }
    }
    auto csv=[&](const char* name)->std::ofstream{return std::ofstream(opt.output/name);};
    auto typ=csv("fx_type_coverage.csv"),feat=csv("fx_feature_coverage.csv"),models=csv("fx_eff_primitives.csv"),
        gap=csv("fx_gap_ranking.csv"),sk=csv("fx_skill_gaps.csv"),dr=csv("fx_table_drift.csv"),
        fails=csv("fx_decode_failures.csv"),coll=csv("fx_basename_collisions.csv"),
        miss=csv("fx_missing_dependencies.csv"),summary=csv("fx_coverage_summary.json");
    if(!typ||!feat||!models||!gap||!sk||!dr||!fails||!coll||!miss||!summary){
        log<<"Could not open CSV or JSON output\n";return 2;
    }
    ba::row(typ,{"type","name","systems","files","skills_reached","assessment","code_evidence","visual_validation","note"});
    ba::row(feat,{"feature","name","systems","files","skills_reached","code_evidence","visual_validation","note"});
    ba::row(models,{"model","model_class","parts","files","skills_reached","visual_validation","note"});
    ba::row(gap,{"gap","kind","assessment","instances","files","skills_reached","code_evidence","visual_validation","note"});
    ba::row(sk,{"skill_id","skill_name","gap_count","gaps"});
    ba::row(dr,{"item","problem","detail"});
    ba::row(fails,{"file","kind","error"});
    ba::row(coll,{"basename","files"});
    ba::row(miss,{"skill_id","referenced_name"});
    auto skcount=[&](const Item& i){return haveDeps?std::to_string(i.skills.size()):"n/a";};
    auto significant=[&](const Item& i){
        if(i.kind=="type")return i.assessment!="fully_verified";
        if(i.kind=="controller_strip")return true;
        if(i.kind=="eff_model")return i.assessment=="unrecognized_model";
        if(i.kind=="feature")return i.evidence=="not_observed"||i.evidence=="parsed_only"||
            i.name.find("model range")!=std::string::npos||i.name.find("filters")!=std::string::npos||
            i.name.find("shade")!=std::string::npos||i.name.find("hit-effect")!=std::string::npos;
        return true;
    };
    std::vector<const Item*> gaps;
    std::map<std::string,std::set<std::string>> perSkill;
    for(const auto& kv:items){
        const auto& i=kv.second;
        if(i.kind=="type")ba::row(typ,{kv.first.substr(5),i.name,std::to_string(i.count),std::to_string(i.files.size()),skcount(i),i.assessment,i.evidence,i.validation,i.note});
        if(i.kind=="feature")ba::row(feat,{kv.first.substr(8),i.name,std::to_string(i.count),std::to_string(i.files.size()),skcount(i),i.evidence,i.validation,i.note});
        if(i.kind=="eff_model")ba::row(models,{i.name,i.assessment,std::to_string(i.count),std::to_string(i.files.size()),skcount(i),i.validation,i.note});
        if(i.count && significant(i)){
            gaps.push_back(&i);
            for(const auto& id:i.skills)perSkill[id].insert(kv.first);
        }
    }
    std::sort(gaps.begin(),gaps.end(),[&](const Item* a,const Item* b){
        if(haveDeps&&a->skills.size()!=b->skills.size())return a->skills.size()>b->skills.size();
        if(a->count!=b->count)return a->count>b->count;
        return a->key<b->key;
    });
    for(const Item* i:gaps){
        ba::row(gap,{i->key,i->kind,i->assessment,std::to_string(i->count),std::to_string(i->files.size()),skcount(*i),i->evidence,i->validation,i->note});
    }
    for(const auto& kv:perSkill){
        std::string ids;for(const auto& id:kv.second){if(!ids.empty())ids+=';';ids+=id;}
        ba::row(sk,{kv.first,skillNames[kv.first],std::to_string(kv.second.size()),ids});
    }
    for(const auto& f:drifts)ba::row(dr,{std::get<0>(f),std::get<1>(f),std::get<2>(f)});
    for(const auto& f:failures)ba::row(fails,{std::get<0>(f),std::get<1>(f),std::get<2>(f)});
    for(const auto& kv:basenameFiles)if(kv.second.size()>1){
        std::string all;for(const auto& path:kv.second){if(!all.empty())all+=';';all+=path;}
        ba::row(coll,{kv.first,all});
    }
    for(const auto& kv:missingLinks)ba::row(miss,{kv.first,kv.second});
    summary<<"{\n  \"schema\": \"FX-COVERAGE-v2\",\n"
           <<"  \"par_files\": "<<parFiles<<",\n  \"eff_files\": "<<effFiles
           <<",\n  \"par_systems\": "<<parSystems<<",\n  \"eff_parts\": "<<effParts
           <<",\n  \"decode_failures\": "<<failures.size()
           <<",\n  \"direct_strip_records\": "<<directStripRecords
           <<",\n  \"unmatched_direct_strip_records\": "<<missingStripRecords
           <<",\n  \"dependency_rows\": "<<dependencyRows
           <<",\n  \"non_effect_dependencies_skipped\": "<<skippedDeps
           <<",\n  \"unmatched_dependency_rows\": "<<unmatched
           <<",\n  \"skill_linkage_available\": "<<(haveDeps?"true":"false")
           <<",\n  \"coverage_gaps\": "<<gaps.size()
           <<",\n  \"skills_with_gaps\": "<<(haveDeps?std::to_string(perSkill.size()):"null")
           <<",\n  \"table_drift\": "<<drift
           <<",\n  \"visual_parity_verified\": false,\n"
           <<"  \"notes\": [\"code branches and fields are evidence only\","
           <<"\"type assessments are manually reviewed approximations\","
           <<"\"direct CMPStrip is imported from a separate inventory if supplied\"]\n}\n";
    log<<"FX coverage: par="<<parFiles<<" eff="<<effFiles<<" systems="<<parSystems
       <<" eff_parts="<<effParts<<" gaps="<<gaps.size()<<" skills_with_gaps="
       <<(haveDeps?std::to_string(perSkill.size()):"unknown")<<" drift="<<drift
       <<" failures="<<failures.size()<<" unmatched_strips="<<missingStripRecords<<" | "<<opt.output<<'\n';
    for(std::size_t i=0;i<gaps.size()&&i<12;++i){
        const auto& g=*gaps[i];
        log<<"  "<<g.key<<" ["<<g.assessment<<"] instances="<<g.count
           <<" skills="<<skcount(g)<<'\n';
    }
    return opt.strict&&(drift||!failures.empty()||missingStripRecords)?1:0;
}
}} // namespace top::fx_coverage
