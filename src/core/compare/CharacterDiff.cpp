#include "core/compare/CharacterDiff.h"
#include "core/unicode/Utf8.h"
#include <vector>
namespace adayo {
namespace {
void Append(std::vector<DiffFragment>& fragments,char32_t cp,DiffKind kind) {
    const auto encoded=unicode::Encode(std::u32string(1,cp));
    if(!fragments.empty() && fragments.back().kind==kind) fragments.back().text+=encoded;
    else fragments.push_back({encoded,kind});
}
}
CharacterDiffResult CharacterDiff::Diff(std::string_view reference_utf8,std::string_view actual_utf8,CompareExecutionContext* context) const {
    return Diff(std::u32string_view(unicode::DecodeStrict(reference_utf8)),std::u32string_view(unicode::DecodeStrict(actual_utf8)),context);
}
CharacterDiffResult CharacterDiff::Diff(std::u32string_view a,std::u32string_view b,CompareExecutionContext* context) const {
    CompareExecutionContext local; if(!context) context=&local; context->Check();
    auto strings=context->Reserve(CompareExecutionContext::Multiply(CompareExecutionContext::Add(a.size(),b.size()),64),"Diff Unicode and output");
    if(a.size()>CompareExecutionContext::record_codepoints || b.size()>CompareExecutionContext::record_codepoints) throw std::runtime_error("Diff record exceeds 65536 codepoints");
    CharacterDiffResult result;
    std::size_t prefix=0,suffix=0;
    while(prefix<a.size() && prefix<b.size() && a[prefix]==b[prefix]) {
        Append(result.reference_fragments,a[prefix],DiffKind::Same); Append(result.actual_fragments,b[prefix],DiffKind::Same); ++prefix;
    }
    while(suffix<a.size()-prefix && suffix<b.size()-prefix && a[a.size()-1-suffix]==b[b.size()-1-suffix]) ++suffix;
    const auto n=a.size()-prefix-suffix,m=b.size()-prefix-suffix;
    const auto cells=CompareExecutionContext::Multiply(n+1,m+1);
    auto memory=context->Reserve(CompareExecutionContext::Multiply(cells,sizeof(std::uint32_t)),"CharacterDiff LCS matrix");
    std::vector<std::uint32_t> lcs(cells);
    auto at=[&](std::size_t i,std::size_t j)->std::uint32_t& {return lcs[i*(m+1)+j];};
    for(std::size_t i=n;i-->0;) {
        context->Check();
        for(std::size_t j=m;j-->0;) {
            if(j%1024==0) context->Check();
            at(i,j)=a[prefix+i]==b[prefix+j] ? 1+at(i+1,j+1) : (std::max)(at(i+1,j),at(i,j+1));
        }
    }
    std::size_t i=0,j=0;
    while(i<n || j<m) {
        context->Check();
        if(i<n && j<m && a[prefix+i]==b[prefix+j]) {
            Append(result.reference_fragments,a[prefix+i++],DiffKind::Same); Append(result.actual_fragments,b[prefix+j++],DiffKind::Same);
        } else if(i<n && (j==m || at(i+1,j)>=at(i,j+1))) Append(result.reference_fragments,a[prefix+i++],DiffKind::Changed);
        else Append(result.actual_fragments,b[prefix+j++],DiffKind::Changed);
    }
    for(std::size_t k=suffix;k>0;--k) {
        Append(result.reference_fragments,a[a.size()-k],DiffKind::Same); Append(result.actual_fragments,b[b.size()-k],DiffKind::Same);
    }
    return result;
}
} // namespace adayo
