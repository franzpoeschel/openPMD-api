import Base

module openPMD
using CxxWrap
@wrapmodule("/home/franzpoeschel/build/nbuild/libopenPMD.jl")

function __init__()
    @initcxx
end
end

s = openPMD.CXX_Series("sample.json", openPMD.ACCESS_CREATE)
function Base.getindex(
    cont::Cont,
    index::Int64,
) where {A,B,Cont<:openPMD.CXX_Container{A,B}}
    return openPMD.cxx_getindex(cont, index)
end

openPMD.cxx_iterations(s)[100]
openPMD.cxx_flush(s, "{}")
