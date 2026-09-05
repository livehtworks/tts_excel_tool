#include "core/compare/TextSimilarity.h"

#include "core/unicode/Utf8.h"

#include <algorithm>
#include <vector>

#ifdef ADAYO_HAS_RAPIDFUZZ
#include <rapidfuzz/fuzz.hpp>
#endif

namespace adayo {
namespace {
std::size_t Lcs(std::u32string_view a, std::u32string_view b, CompareExecutionContext& context) {
    if (a.size() > b.size()) return Lcs(b, a, context);
    auto memory=context.Reserve(CompareExecutionContext::Multiply(a.size()+1,sizeof(std::size_t)*2),"Indel rolling rows");
    std::vector<std::size_t> prev(a.size() + 1), cur(a.size() + 1);
    for (std::size_t j = 1; j <= b.size(); ++j) {
        context.Check();
        cur[0] = 0;
        for (std::size_t i = 1; i <= a.size(); ++i) {
            if((i%1024)==0) context.Check();
            cur[i]=a[i-1]==b[j-1] ? prev[i-1]+1 : (std::max)(cur[i-1],prev[i]);
        }
        std::swap(prev, cur);
    }
    return prev[a.size()];
}
}

double TextSimilarity::Ratio(std::string_view left_utf8, std::string_view right_utf8) const {
    const auto left = unicode::DecodeStrict(left_utf8);
    const auto right = unicode::DecodeStrict(right_utf8);
    return Ratio(std::u32string_view(left),std::u32string_view(right));
}
double TextSimilarity::Ratio(std::u32string_view left,std::u32string_view right,CompareExecutionContext* context) const {
    CompareExecutionContext local;
    if(!context) context=&local;
    context->Check();
    if(left.size()>CompareExecutionContext::record_codepoints || right.size()>CompareExecutionContext::record_codepoints) throw std::runtime_error("Record exceeds 65536 codepoints");
    if(left==right) return 100.0;
    if (left.empty() && right.empty()) return 100.0;
    if (left.empty() || right.empty()) return 0.0;
#ifdef ADAYO_HAS_RAPIDFUZZ
    return rapidfuzz::fuzz::ratio(left, right);
#else
    const auto length=left.size()+right.size();
    std::size_t common=0;
    while(!left.empty() && !right.empty() && left.front()==right.front()) { ++common; left.remove_prefix(1); right.remove_prefix(1); }
    while(!left.empty() && !right.empty() && left.back()==right.back()) { ++common; left.remove_suffix(1); right.remove_suffix(1); }
    return 200.0*static_cast<double>(common+Lcs(left,right,*context))/static_cast<double>(length);
#endif
}

} // namespace adayo
