using Microsoft.CodeAnalysis;
using System.Text;

static class Names
{
    public const string Asm = "TowerFall";

    static readonly HashSet<string> Reserved = new()
    {
        "alignas","alignof","and","and_eq","asm","auto","bitand","bitor","bool","break","case","catch","char","char8_t","char16_t","char32_t",
        "class","compl","concept","const","consteval","constexpr","constinit","const_cast","continue","co_await","co_return","co_yield","decltype",
        "default","delete","do","double","dynamic_cast","else","enum","explicit","export","extern","false","float","for","friend","goto","if",
        "inline","int","long","mutable","namespace","new","noexcept","not","not_eq","nullptr","operator","or","or_eq","private","protected",
        "public","register","reinterpret_cast","requires","return","short","signed","sizeof","static","static_assert","static_cast","struct",
        "switch","template","this","thread_local","throw","true","try","typedef","typeid","typename","union","unsigned","using","virtual",
        "void","volatile","wchar_t","while","xor","xor_eq","final","override",
        // common macros / libc names that can collide
        "NULL","EOF","TRUE","FALSE","PI","DEBUG","errno","stdin","stdout","stderr","minor","major","unix","linux","assert","BIG_ENDIAN",
        "LITTLE_ENDIAN","BYTE_ORDER","INFINITY","NAN","EXIT_SUCCESS","EXIT_FAILURE","RAND_MAX","CHAR_BIT","INT_MAX","INT_MIN","FLT_MAX",
        "M_PI","small","near","far","interface","offsetof","signal","index","rindex",
        "exp","log","sin","cos","tan","abs","floor","ceil","round","sqrt","pow","min","max","time","clock","random","rand","select",
        "printf","puts","read","write","open","close","stat","access","remove","rename","exit","abort","free","malloc","alloca","main",
        "MAX","MIN","ABS","SIGN","DOMAIN","OVERFLOW","UNDERFLOW","TLOSS","PLOSS",
    };

    public static string Id(string name)
    {
        // C# verbatim identifiers / compiler generated names
        name = name.Replace("<", "_").Replace(">", "_").Replace("$", "_").Replace(".", "_");
        if (Reserved.Contains(name)) return name + "_";
        if (name.StartsWith("__")) return "x" + name;
        return name;
    }

    public static bool IsOurs(ISymbol s) => s.ContainingAssembly?.Name == Asm;

    public static string Ns(INamespaceSymbol ns)
    {
        if (ns == null || ns.IsGlobalNamespace) return "";
        var p = Ns(ns.ContainingNamespace);
        return p + "::" + ns.Name;
    }

    // Flattened simple name of a named type (Outer_Inner), no namespace, no type args
    public static string FlatName(INamedTypeSymbol t)
    {
        var n = t.Name;
        if (t.ContainingType != null) n = FlatName(t.ContainingType) + "_" + n;
        return n;
    }

    static IEnumerable<ITypeSymbol> AllTypeArgs(INamedTypeSymbol t)
    {
        if (t.ContainingType != null) foreach (var a in AllTypeArgs(t.ContainingType)) yield return a;
        foreach (var a in t.TypeArguments) yield return a;
    }

    public static bool IsDelegate(ITypeSymbol t) => t.TypeKind == TypeKind.Delegate;

    public static string Prim(ITypeSymbol t)
    {
        switch (t.SpecialType)
        {
            case SpecialType.System_Void: return "void";
            case SpecialType.System_Boolean: return "bool";
            case SpecialType.System_Byte: return "uint8_t";
            case SpecialType.System_SByte: return "int8_t";
            case SpecialType.System_Int16: return "int16_t";
            case SpecialType.System_UInt16: return "uint16_t";
            case SpecialType.System_Int32: return "int32_t";
            case SpecialType.System_UInt32: return "uint32_t";
            case SpecialType.System_Int64: return "int64_t";
            case SpecialType.System_UInt64: return "uint64_t";
            case SpecialType.System_Single: return "float";
            case SpecialType.System_Double: return "double";
            case SpecialType.System_Char: return "char16_t";
            case SpecialType.System_IntPtr: return "intptr_t";
            case SpecialType.System_UIntPtr: return "uintptr_t";
            case SpecialType.System_Decimal: return "double";
        }
        return null;
    }

    public static bool IsPrimitive(ITypeSymbol t) => Prim(t) != null && t.SpecialType != SpecialType.System_Void;

    // The C++ class name (no pointer) for a named type
    public static string ClassName(ITypeSymbol t)
    {
        if (t is ITypeParameterSymbol tp) return tp.Name;
        if (t is IArrayTypeSymbol at)
        {
            if (at.Rank == 1) return "::System::Array<" + Type(at.ElementType) + ">";
            return "::System::Array" + at.Rank + "<" + Type(at.ElementType) + ">";
        }
        if (t.SpecialType == SpecialType.System_Object) return "::System::Object";
        if (t.SpecialType == SpecialType.System_String) return "::System::String";
        if (t.SpecialType == SpecialType.System_Array) return "::System::ArrayBase";
        if (t.SpecialType == SpecialType.System_Delegate || t.SpecialType == SpecialType.System_MulticastDelegate) return "::sys::Delegate";
        var p = Prim(t);
        if (p != null) return p;
        if (t is INamedTypeSymbol nt)
        {
            if (IsDelegate(nt)) return DelegateClass(nt);
            if (nt.OriginalDefinition.SpecialType == SpecialType.System_Nullable_T)
                return "::System::Nullable<" + Type(nt.TypeArguments[0]) + ">";
            var sb = new StringBuilder();
            sb.Append(Ns(nt.ContainingNamespace)).Append("::").Append(Id(FlatName(nt)));
            var args = nt.TypeKind == TypeKind.Enum ? new List<ITypeSymbol>() : AllTypeArgs(nt).ToList();
            if (args.Count > 0)
            {
                sb.Append('<').Append(string.Join(", ", args.Select(Type))).Append('>');
            }
            return sb.ToString();
        }
        throw new Exception("Unknown type " + t);
    }

    public static string DelegateClass(INamedTypeSymbol d)
    {
        var inv = d.DelegateInvokeMethod;
        return "::sys::Fn<" + Sig(inv) + ">";
    }

    public static string Sig(IMethodSymbol m)
    {
        return Type(m.ReturnType) + "(" + string.Join(", ", m.Parameters.Select(ParamType)) + ")";
    }

    public static string ParamType(IParameterSymbol p)
    {
        var t = Type(p.Type);
        if (p.RefKind != RefKind.None) t += "&";
        return t;
    }

    public static bool IsRef(ITypeSymbol t)
    {
        if (t is ITypeParameterSymbol) return false; // template param carries pointer itself
        if (t.SpecialType == SpecialType.System_Void) return false;
        return t.IsReferenceType;
    }

    // Is a variable of this C++ type accessed with -> ?
    public static bool IsPointerLike(ITypeSymbol t)
    {
        if (t is ITypeParameterSymbol tp)
            return tp.HasReferenceTypeConstraint || tp.ConstraintTypes.Any(c => c.TypeKind == TypeKind.Class || c.TypeKind == TypeKind.Interface);
        return t.IsReferenceType;
    }

    public static string Type(ITypeSymbol t)
    {
        var p = Prim(t);
        if (p != null) return p;
        if (t is ITypeParameterSymbol tp) return tp.Name;
        var c = ClassName(t);
        if (t.IsReferenceType) return c + "*";
        return c;
    }

    public static bool IsInterface(ITypeSymbol t) => t.TypeKind == TypeKind.Interface;

    public static string MemberName(ISymbol s)
    {
        var n = s.Name;
        if (s is IMethodSymbol m)
        {
            if (m.MethodKind == MethodKind.ExplicitInterfaceImplementation)
            {
                var im = m.ExplicitInterfaceImplementations.First();
                n = im.Name;
            }
            if (m.MethodKind == MethodKind.Conversion)
            {
                n = m.Name + "__" + Id(SafeTypeTag(m.ReturnType));
            }
        }
        if (s is IPropertySymbol ps && ps.ExplicitInterfaceImplementations.Length > 0) n = ps.ExplicitInterfaceImplementations[0].Name;
        return Id(n);
    }

    public static string SafeTypeTag(ITypeSymbol t)
    {
        return new string(t.ToDisplayString().Select(ch => char.IsLetterOrDigit(ch) ? ch : '_').ToArray());
    }

    public static string Escape16(string s)
    {
        var sb = new StringBuilder();
        foreach (var ch in s)
        {
            if (ch == '\\') sb.Append("\\\\");
            else if (ch == '"') sb.Append("\\\"");
            else if (ch == '\n') sb.Append("\\n");
            else if (ch == '\r') sb.Append("\\r");
            else if (ch == '\t') sb.Append("\\t");
            else if (ch == '?') sb.Append("\\?");
            else if (ch < 32 || ch > 126) sb.Append("\\u").Append(((int)ch).ToString("X4"));
            else sb.Append(ch);
        }
        return sb.ToString();
    }
}
