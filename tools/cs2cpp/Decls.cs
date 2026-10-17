using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using System.Text;

// Emits headers: forward decls, enums, structs, classes, templates
class Decls
{
    public readonly Compilation Comp;
    public readonly List<INamedTypeSymbol> Types = new();
    public readonly Dictionary<INamedTypeSymbol, SyntaxTree> TypeTree = new(SymbolEqualityComparer.Default);
    public Bodies Bodies;

    public Decls(Compilation comp)
    {
        Comp = comp;
        foreach (var t in comp.SyntaxTrees)
        {
            var m = comp.GetSemanticModel(t);
            foreach (var td in t.GetRoot().DescendantNodes().OfType<BaseTypeDeclarationSyntax>())
            {
                var s = (INamedTypeSymbol)m.GetDeclaredSymbol(td);
                Types.Add(s);
                TypeTree[s] = t;
            }
            foreach (var dd in t.GetRoot().DescendantNodes().OfType<DelegateDeclarationSyntax>())
            {
                // delegates map to sys::Fn, nothing to declare
            }
        }
    }

    // ---------- property classification ----------
    static readonly Dictionary<IPropertySymbol, bool> fieldLikeCache = new(SymbolEqualityComparer.Default);
    public static bool IsFieldLike(IPropertySymbol p)
    {
        p = p.OriginalDefinition;
        if (fieldLikeCache.TryGetValue(p, out var r)) return r;
        r = ComputeFieldLike(p);
        fieldLikeCache[p] = r;
        return r;
    }
    static bool ComputeFieldLike(IPropertySymbol p)
    {
        if (!Names.IsOurs(p)) return false;
        if (p.IsIndexer || p.IsVirtual || p.IsAbstract || p.IsOverride) return false;
        if (p.ExplicitInterfaceImplementations.Length > 0) return false;
        if (p.ContainingType.TypeKind == TypeKind.Interface) return false;
        // implements an interface member implicitly?
        foreach (var iface in p.ContainingType.AllInterfaces)
            foreach (var im in iface.GetMembers().OfType<IPropertySymbol>())
                if (SymbolEqualityComparer.Default.Equals(p.ContainingType.FindImplementationForInterfaceMember(im), p)) return false;
        var decl = p.DeclaringSyntaxReferences.FirstOrDefault()?.GetSyntax() as PropertyDeclarationSyntax;
        if (decl == null) return false;
        if (decl.ExpressionBody != null) return false;
        if (decl.AccessorList == null) return false;
        foreach (var a in decl.AccessorList.Accessors)
            if (a.Body != null || a.ExpressionBody != null) return false;
        return true;
    }

    public static bool IsAutoProp(IPropertySymbol p)
    {
        if (!Names.IsOurs(p) || p.IsAbstract || p.ContainingType.TypeKind == TypeKind.Interface) return false;
        var decl = p.DeclaringSyntaxReferences.FirstOrDefault()?.GetSyntax() as PropertyDeclarationSyntax;
        if (decl == null || decl.ExpressionBody != null || decl.AccessorList == null) return false;
        return decl.AccessorList.Accessors.All(a => a.Body == null && a.ExpressionBody == null);
    }

    public static bool IsPrimaryCtor(IMethodSymbol c) =>
        c.MethodKind == MethodKind.Constructor && !c.IsImplicitlyDeclared && c.DeclaringSyntaxReferences.FirstOrDefault()?.GetSyntax() is TypeDeclarationSyntax;

    public static bool IsFieldLikeEvent(IEventSymbol e) => true;

    // ---------- helpers ----------
    public static string Kw(INamedTypeSymbol t) => "struct";

    public static IEnumerable<ISymbol> Members(INamedTypeSymbol t) => t.GetMembers().Where(m => !m.IsImplicitlyDeclared || m is IMethodSymbol { MethodKind: MethodKind.Constructor });

    public bool HasStaticInit(INamedTypeSymbol t)
    {
        if (t.TypeKind == TypeKind.Enum || t.TypeKind == TypeKind.Interface) return false;
        if (t.StaticConstructors.Any(c => !c.IsImplicitlyDeclared)) return true;
        foreach (var m in t.GetMembers())
        {
            if (m is IFieldSymbol f && f.IsStatic && !f.IsConst && FieldInitializer(f) != null) return true;
            if (m is IPropertySymbol p && p.IsStatic && IsFieldLike(p) && PropInitializer(p) != null) return true;
        }
        return false;
    }

    public static EqualsValueClauseSyntax FieldInitializer(IFieldSymbol f)
    {
        var d = f.DeclaringSyntaxReferences.FirstOrDefault()?.GetSyntax() as VariableDeclaratorSyntax;
        return d?.Initializer;
    }
    public static EqualsValueClauseSyntax PropInitializer(IPropertySymbol p)
    {
        var d = p.DeclaringSyntaxReferences.FirstOrDefault()?.GetSyntax() as PropertyDeclarationSyntax;
        return d?.Initializer;
    }
    public static EqualsValueClauseSyntax EventInitializer(IEventSymbol e)
    {
        var d = e.DeclaringSyntaxReferences.FirstOrDefault()?.GetSyntax() as VariableDeclaratorSyntax;
        return d?.Initializer;
    }

    public string TemplateHeader(INamedTypeSymbol t)
    {
        if (t.TypeKind == TypeKind.Enum) return "";
        var ps = AllTypeParams(t).ToList();
        if (ps.Count == 0) return "";
        return "template<" + string.Join(", ", ps.Select(p => "typename " + p.Name)) + "> ";
    }
    public static IEnumerable<ITypeParameterSymbol> AllTypeParams(INamedTypeSymbol t)
    {
        if (t.ContainingType != null) foreach (var p in AllTypeParams(t.ContainingType)) yield return p;
        foreach (var p in t.TypeParameters) yield return p;
    }
    public static bool IsGenericType(INamedTypeSymbol t) => AllTypeParams(t).Any();

    // ---------- ordering ----------
    List<INamedTypeSymbol> Order(IEnumerable<INamedTypeSymbol> set, Func<INamedTypeSymbol, IEnumerable<INamedTypeSymbol>> deps)
    {
        var list = set.ToList();
        var inSet = new HashSet<INamedTypeSymbol>(list, SymbolEqualityComparer.Default);
        var done = new HashSet<INamedTypeSymbol>(SymbolEqualityComparer.Default);
        var res = new List<INamedTypeSymbol>();
        void Visit(INamedTypeSymbol t, int depth)
        {
            if (done.Contains(t) || depth > 200) return;
            done.Add(t);
            foreach (var d in deps(t)) { var o = d.OriginalDefinition; if (inSet.Contains(o)) Visit(o, depth + 1); }
            res.Add(t);
        }
        foreach (var t in list.OrderBy(x => x.ToDisplayString())) Visit(t, 0);
        return res;
    }

    IEnumerable<INamedTypeSymbol> StructDeps(INamedTypeSymbol t)
    {
        foreach (var f in t.GetMembers().OfType<IFieldSymbol>())
            if (!f.IsStatic && f.Type is INamedTypeSymbol nt && nt.IsValueType) { yield return nt; foreach (var a in nt.TypeArguments.OfType<INamedTypeSymbol>()) yield return a; }
        foreach (var p in t.GetMembers().OfType<IPropertySymbol>())
            if (!p.IsStatic && IsFieldLike(p) && p.Type is INamedTypeSymbol nt && nt.IsValueType) yield return nt;
    }
    IEnumerable<INamedTypeSymbol> ClassDeps(INamedTypeSymbol t)
    {
        if (t.BaseType != null) { yield return t.BaseType; foreach (var a in t.BaseType.TypeArguments.OfType<INamedTypeSymbol>()) yield return a; }
        foreach (var i in t.Interfaces) yield return i;
    }

    // ---------- emit ----------
    public string EmitFwd()
    {
        var sb = new StringBuilder();
        sb.AppendLine("// Generated by cs2cpp. Do not edit.");
        sb.AppendLine("#pragma once");
        sb.AppendLine("#include \"sys/runtime.h\"");
        foreach (var g in Types.Where(t => t.TypeKind != TypeKind.Enum && t.TypeKind != TypeKind.Delegate).GroupBy(t => Names.Ns(t.ContainingNamespace)))
        {
            sb.AppendLine(OpenNs(g.Key));
            foreach (var t in g) sb.AppendLine("  " + TemplateHeader(t) + "struct " + Names.Id(Names.FlatName(t)) + ";");
            sb.AppendLine(CloseNs(g.Key));
        }
        // enums
        foreach (var g in Types.Where(t => t.TypeKind == TypeKind.Enum).GroupBy(t => Names.Ns(t.ContainingNamespace)))
        {
            sb.AppendLine(OpenNs(g.Key));
            foreach (var t in g)
            {
                sb.AppendLine("  enum class " + Names.Id(Names.FlatName(t)) + " : " + Names.Prim(t.EnumUnderlyingType) + " {");
                foreach (var f in t.GetMembers().OfType<IFieldSymbol>().Where(f => f.IsConst))
                    sb.AppendLine("    " + Names.Id(f.Name) + " = " + Lit.Int(f.ConstantValue, t.EnumUnderlyingType) + ",");
                sb.AppendLine("  };");
            }
            sb.AppendLine(CloseNs(g.Key));
        }
        foreach (var t in Types.Where(t => t.TypeKind == TypeKind.Enum))
        {
            var cn = Names.ClassName(t);
            var flags = t.GetAttributes().Any(a => a.AttributeClass?.Name == "FlagsAttribute");
            sb.Append("SYS_ENUM_INFO(" + cn + ", " + (flags ? "true" : "false"));
            foreach (var f in t.GetMembers().OfType<IFieldSymbol>().Where(f => f.IsConst))
                sb.Append(", " + cn + "::" + Names.Id(f.Name) + ", u\"" + f.Name + "\"");
            sb.AppendLine(")");
        }
        return sb.ToString();
    }

    public static string OpenNs(string ns)
    {
        if (ns == "") return "";
        return "namespace " + ns.Substring(2) + " {";
    }
    public static string CloseNs(string ns) => ns == "" ? "" : "}";

    public string EmitTypes()
    {
        var sb = new StringBuilder();
        sb.AppendLine("// Generated by cs2cpp. Do not edit.");
        sb.AppendLine("#pragma once");
        sb.AppendLine("#include \"tf_fwd.h\"");
        var structs = Order(Types.Where(t => t.TypeKind == TypeKind.Struct), StructDeps);
        var ifaces = Order(Types.Where(t => t.TypeKind == TypeKind.Interface), ClassDeps);
        var classes = Order(Types.Where(t => t.TypeKind == TypeKind.Class), ClassDeps);
        foreach (var t in structs.Concat(ifaces).Concat(classes)) EmitTypeDecl(sb, t);
        return sb.ToString();
    }

    void EmitTypeDecl(StringBuilder sb, INamedTypeSymbol t)
    {
        var ns = Names.Ns(t.ContainingNamespace);
        sb.AppendLine(OpenNs(ns));
        var name = Names.Id(Names.FlatName(t));
        var bases = new List<string>();
        if (t.TypeKind == TypeKind.Class)
        {
            if (t.BaseType != null) bases.Add("public " + Names.ClassName(t.BaseType));
            foreach (var i in t.Interfaces) if (!t.BaseType.AllInterfaces.Contains(i, SymbolEqualityComparer.Default)) bases.Add("public " + Names.ClassName(i));
        }
        else if (t.TypeKind == TypeKind.Interface)
        {
            foreach (var i in t.Interfaces) bases.Add("public " + Names.ClassName(i));
            if (t.Interfaces.Length == 0) bases.Add("public ::System::Interface");
        }
        else if (t.TypeKind == TypeKind.Struct)
        {
            foreach (var i in t.Interfaces) { /* struct interfaces ignored */ }
        }
        sb.Append(TemplateHeader(t)).Append("struct ").Append(name);
        if (bases.Count > 0) sb.Append(" : ").Append(string.Join(", ", bases));
        sb.AppendLine(" {");
        if (t.TypeKind == TypeKind.Class && t.BaseType != null)
            sb.AppendLine("  typedef " + Names.ClassName(t.BaseType) + " base;");
        bool isStruct = t.TypeKind == TypeKind.Struct;
        bool isIface = t.TypeKind == TypeKind.Interface;
        bool generic = IsGenericType(t);
        // fields
        foreach (var m in t.GetMembers())
        {
            if (m is IFieldSymbol f)
            {
                if (f.IsConst) continue;
                if (f.IsImplicitlyDeclared) continue;
                sb.AppendLine("  " + (f.IsStatic ? "static " : "") + Names.Type(f.Type) + " " + Names.Id(f.Name) + (f.IsStatic ? ";" : "{};"));
            }
            else if (m is IPropertySymbol p && IsFieldLike(p))
            {
                sb.AppendLine("  " + (p.IsStatic ? "static " : "") + Names.Type(p.Type) + " " + Names.Id(p.Name) + (p.IsStatic ? ";" : "{};"));
            }
            else if (m is IEventSymbol e && !isIface)
            {
                sb.AppendLine("  " + (e.IsStatic ? "static " : "") + Names.Type(e.Type) + " " + Names.Id(e.Name) + (e.IsStatic ? ";" : "{};"));
            }
        }
        foreach (var pc in t.InstanceConstructors.Where(IsPrimaryCtor))
            foreach (var p in pc.Parameters)
                sb.AppendLine("  " + Names.Type(p.Type) + " __pc_" + Names.Id(p.Name) + "{};");
        if (HasStaticInit(t))
        {
            sb.AppendLine("  static bool __cinit_done;");
            sb.AppendLine("  static void __cinit_run();");
            sb.AppendLine("  static inline void __cinit() { if (!__cinit_done) __cinit_run(); }");
        }
        // constructors
        if (isStruct) sb.AppendLine("  " + name + "() = default;");
        bool needsInitCtor = !isStruct && !isIface && t.InstanceConstructors.All(c => c.IsImplicitlyDeclared);
        foreach (var c in t.InstanceConstructors)
        {
            if (!isStruct)
            {
                if (!isIface) sb.AppendLine("  " + Names.ClassName(t) + "* __ctor(" + (c.IsImplicitlyDeclared ? "" : Params(c)) + ");");
                continue;
            }
            if (c.IsImplicitlyDeclared) continue;
            if (c.Parameters.Length == 0) continue;
            sb.AppendLine("  " + (c.Parameters.Length == 1 ? "explicit " : "") + name + "(" + Params(c) + ");");
        }
        if (t.TypeKind == TypeKind.Class && !t.IsStatic) sb.AppendLine("  virtual ~" + name + "() = default;");
        // methods
        foreach (var m in t.GetMembers().OfType<IMethodSymbol>())
        {
            if (m.MethodKind is MethodKind.Constructor or MethodKind.StaticConstructor or MethodKind.Destructor) continue;
            if (m.IsImplicitlyDeclared) continue;
            if (m.AssociatedSymbol is IPropertySymbol ap && IsFieldLike(ap)) continue;
            if (m.AssociatedSymbol is IEventSymbol) continue;
            EmitMethodDecl(sb, m, isIface);
        }
        // backing fields for non-field-like auto properties
        foreach (var p in t.GetMembers().OfType<IPropertySymbol>())
            if (!IsFieldLike(p) && IsAutoProp(p))
                sb.AppendLine("  " + (p.IsStatic ? "static " : "") + Names.Type(p.Type) + " __p_" + Names.Id(p.Name) + (p.IsStatic ? ";" : "{};"));
        // using-declarations to unhide base overloads
        if (t.TypeKind == TypeKind.Class && t.BaseType != null)
        {
            var declared = t.GetMembers().OfType<IMethodSymbol>().Where(m => m.MethodKind == MethodKind.Ordinary && !m.IsImplicitlyDeclared).Select(m => Names.MemberName(m)).Distinct();
            foreach (var n in declared)
            {
                bool inBase = false;
                for (var b = t.BaseType; b != null; b = b.BaseType)
                    if (b.GetMembers().OfType<IMethodSymbol>().Any(m => m.MethodKind == MethodKind.Ordinary && Names.MemberName(m) == n && m.DeclaredAccessibility != Accessibility.Private)) { inBase = true; break; }
                if (inBase) sb.AppendLine("  using base::" + n + ";");
            }
        }
        sb.AppendLine("};");
        sb.AppendLine(CloseNs(ns));
    }

    public static string Params(IMethodSymbol m) => string.Join(", ", m.Parameters.Select(p => Names.ParamType(p) + " " + Names.Id(p.Name)));

    public static string MethodTemplate(IMethodSymbol m)
    {
        if (!m.IsGenericMethod) return "";
        return "template<" + string.Join(", ", m.TypeParameters.Select(p => "typename " + p.Name)) + "> ";
    }

    void EmitMethodDecl(StringBuilder sb, IMethodSymbol m, bool isIface)
    {
        var name = Names.MemberName(m);
        var ret = Names.Type(m.ReturnType);
        if (m.ReturnsByRef) ret += "&";
        var pre = "  " + MethodTemplate(m);
        bool isStructT = m.ContainingType.TypeKind == TypeKind.Struct;
        bool isVirt = (m.IsVirtual || m.IsAbstract || m.IsOverride || isIface || ImplementsInterface(m)) && !m.IsStatic && !m.IsGenericMethod && !isStructT;
        if (m.IsStatic) pre += "static ";
        else if (isVirt) pre += "virtual ";
        var post = "";
        if (m.IsOverride && !isStructT) post = " override";
        if ((m.IsAbstract || isIface) && !m.IsStatic) post += " = 0";
        sb.AppendLine(pre + ret + " " + name + "(" + Params(m) + ")" + post + ";");
        if (m.IsIterator)
            sb.AppendLine("  " + MethodTemplate(m) + (m.IsStatic ? "static " : "") + Bodies.GenType(m) + " " + name + "__it(" + Params(m) + ");");
    }

    public static bool ImplementsInterface(IMethodSymbol m)
    {
        if (m.ExplicitInterfaceImplementations.Length > 0) return true;
        foreach (var iface in m.ContainingType.AllInterfaces)
            foreach (var im in iface.GetMembers().OfType<IMethodSymbol>())
                if (SymbolEqualityComparer.Default.Equals(m.ContainingType.FindImplementationForInterfaceMember(im), m)) return true;
        return false;
    }
}

static class Lit
{
    public static string Int(object v, ITypeSymbol t)
    {
        switch (v)
        {
            case int i: return i == int.MinValue ? "(-2147483647-1)" : i.ToString();
            case uint u: return u + "u";
            case long l: return l == long.MinValue ? "(-9223372036854775807LL-1)" : l + "LL";
            case ulong ul: return ul + "ULL";
            case short s: return s.ToString();
            case ushort us: return us.ToString();
            case byte b: return b.ToString();
            case sbyte sb: return sb.ToString();
            case char c: return ((int)c).ToString();
        }
        return Convert.ToInt64(v).ToString();
    }

    public static string Float(float f)
    {
        if (float.IsNaN(f)) return "::sys::nanf()";
        if (float.IsPositiveInfinity(f)) return "::sys::inff()";
        if (float.IsNegativeInfinity(f)) return "(-::sys::inff())";
        var s = f.ToString("R", System.Globalization.CultureInfo.InvariantCulture);
        if (!s.Contains('.') && !s.Contains('E')) s += ".0";
        if (s.Contains('E') && !s.Contains('.')) s = s.Replace("E", ".0E");
        return s + "f";
    }
    public static string Double(double f)
    {
        if (double.IsNaN(f)) return "::sys::nan()";
        if (double.IsPositiveInfinity(f)) return "::sys::inf()";
        if (double.IsNegativeInfinity(f)) return "(-::sys::inf())";
        var s = f.ToString("R", System.Globalization.CultureInfo.InvariantCulture);
        if (!s.Contains('.') && !s.Contains('E')) s += ".0";
        if (s.Contains('E') && !s.Contains('.')) s = s.Replace("E", ".0E");
        return s;
    }
}
