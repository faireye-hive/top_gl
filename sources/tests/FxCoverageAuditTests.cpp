// SK2.14 - generates small historical-format .par fixtures; no Client assets
// or OpenGL context required. Run with asserts enabled (Debug build).
#include "top/FxCoverageAudit.h"
#include <stdexcept>
#define CHECK(expr) do { if (!(expr)) throw std::runtime_error("check failed: " #expr); } while (0)
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <cstring>

namespace fs=std::filesystem;
namespace fx=top::fx_coverage;
static void write32(std::ostream& out,const std::string& value){
    char buffer[32]{};std::memcpy(buffer,value.data(),std::min<std::size_t>(31,value.size()));
    out.write(buffer,32);
}
template<class T> static void w(std::ostream& out,T v){out.write(reinterpret_cast<const char*>(&v),sizeof(v));}
static void par(const fs::path& file,int type,std::string model=""){
    std::ofstream out(file,std::ios::binary);
    w<uint32_t>(out,2); write32(out,"test_controller");w<int32_t>(out,1);
    w<int32_t>(out,type);write32(out,"test_system");w<int32_t>(out,2);
    write32(out,"a.tga");write32(out,model);
    for(int i=0;i<3;++i)w<float>(out,float(i));
    w<uint16_t>(out,2);
    for(int i=0;i<2;++i)w<float>(out,1.f);
    for(int i=0;i<6;++i)w<float>(out,0.f);
    for(int i=0;i<8;++i)w<float>(out,1.f);
    w<uint8_t>(out,1);
    w<int32_t>(out,5);w<int32_t>(out,6);w<int32_t>(out,1);w<int32_t>(out,1);
    w<float>(out,0.5f);w<float>(out,1.f);
    for(int i=0;i<3;++i)w<float>(out,0.f);
    for(int i=0;i<3;++i)w<float>(out,0.f);
    w<float>(out,.1f);
}
static std::string read(const fs::path& file){
    std::ifstream in(file);return {std::istreambuf_iterator<char>(in),{}};
}
int main(){
    CHECK(fx::normalizedBase("6.par")=="00000006.par");
    CHECK(fx::normalizedBase("effect/WOLF01.EFF")=="wolf01.eff");
    CHECK(fx::classification("RectPlane")=="native_primitive");
    CHECK(fx::classification("other.lgo")=="external_lgo");
    CHECK(fx::classification("unknown")=="unrecognized_model");
    CHECK(top::blend_audit::outsideClient("Client","build/fx-coverage"));
    auto src=fx::readSource("sources/Engine/src/top/EffectSystem.cpp");
    CHECK(src.valid);
    CHECK(!fx::findType(src,2).outside.empty());
    CHECK(!fx::findType(src,8).outside.empty());
    const auto tmp=fs::temp_directory_path()/"top_fx_coverage_test_SK214";
    fs::remove_all(tmp);fs::create_directories(tmp/"Client/effect");
    par(tmp/"Client/effect/fire.par",2);
    par(tmp/"Client/effect/00000006.par",8);
    par(tmp/"Client/effect/dummy.par",13);
    {std::ofstream f(tmp/"deps.csv");f<<"skill_id,skill_name,channel,referenced_name\n"
        <<"81,Illusion Slash,cast,6.par\n"
        <<"84,Berserk,cast,fire.par\n"
        <<"84,Berserk,cast,dummy.par\n"
        <<"42,Unmatched,cast,absent.par\n";}
    {std::ofstream f(tmp/"strips.csv");f<<"file,index,max_length,dummy_a,dummy_b,life,step,texture,src_blend,dst_blend,implementation\n"
        <<"effect/00000006.par,0,100,0,1,0.5,0.03,swordlight02,5,2,not_rendered\n";}
    fx::Options opts;
    opts.client=tmp/"Client";opts.output=tmp/"out";
    opts.deps=tmp/"deps.csv";
    opts.directStrips=tmp/"strips.csv";
    std::ostringstream log;
    CHECK(fx::run(opts,log)==0);
    auto summary=read(opts.output/"fx_coverage_summary.json");
    CHECK(summary.find("\"par_systems\": 3")!=std::string::npos);
    CHECK(summary.find("\"direct_strip_records\": 1")!=std::string::npos);
    CHECK(summary.find("\"table_drift\": 0")!=std::string::npos);
    CHECK(read(opts.output/"fx_skill_gaps.csv").find("81,Illusion Slash")!=std::string::npos);
    CHECK(read(opts.output/"fx_gap_ranking.csv").find("direct_cmpstrip")!=std::string::npos);
    // SK2.15-A: the auditor must not continue reporting "not rendered" after
    // the renderer adds genuine two-dummy CMPStrip geometry.
    CHECK(read(opts.output/"fx_gap_ranking.csv").find("partial_anchor_dependent")!=std::string::npos);
    CHECK(read(opts.output/"fx_missing_dependencies.csv").find("absent.par")!=std::string::npos);
    CHECK(read(opts.output/"fx_type_coverage.csv").find("8,ARRAW,1,1,1,approximate")!=std::string::npos);
    opts.strict=true;
    std::ostringstream strictLog;
    CHECK(fx::run(opts,strictLog)==0);
    par(tmp/"Client/effect/rogue.par",99);
    std::ostringstream driftLog;
    CHECK(fx::run(opts,driftLog)==1);
    CHECK(read(opts.output/"fx_table_drift.csv").find("type:99")!=std::string::npos);
    opts.strict=false; opts.output=opts.client/"reports";
    std::ostringstream safeLog;
    CHECK(fx::run(opts,safeLog)==2);
    fs::remove_all(tmp);
}
