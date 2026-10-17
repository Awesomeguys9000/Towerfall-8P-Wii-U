using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Microsoft.CodeAnalysis.Operations;
using System.Text;

class Ctx
{
    public INamedTypeSymbol Type;
    public IMethodSymbol Method;
    public SemanticModel Model;
    public HashSet<ISymbol> Boxed = new(SymbolEqualityComparer.Default);
    public bool InStaticContext;
    public bool IsIterator;
    public Dictionary<ILabelSymbol, string> Labels = new(SymbolEqualityComparer.Default);
    public HashSet<ILabelSymbol> GotoTargets = new(SymbolEqualityComparer.Default);
    public Stack<(ILabelSymbol exit, string gotoName)> SwitchExits = new();
    public Stack<string> ImplicitReceivers = new();
    public string CondAccessVar;
    public int Tmp;
    public HashSet<ILocalSymbol> Predeclared = new(SymbolEqualityComparer.Default);
}

partial class Bodies
{
    readonly Compilation comp;
    readonly Decls decls;
    Ctx C;
    public List<string> Warnings = new();
    public HashSet<string> ExternalUses = new();

    public Bodies(Compilation comp, Decls decls) { this.comp = comp; this.decls = decls; }

    string T(ITypeSymbol t) => Names.Type(t);
    string CN(ITypeSymbol t) => Names.ClassName(t);
    string Tmp(string p = "__t") => p + (C.Tmp++);

    public static string GenType(IMethodSymbol m)
    {
        var rt = (INamedTypeSymbol)m.ReturnType;
        if (rt.IsGenericType) return "::sys::Gen<" + Names.Type(rt.TypeArguments[0]) + ">";
        return "::sys::Gen<::System::Object*>";
    }

    // ===================== file emission =====================
    public (string cpp, string tmpl) EmitFile(SyntaxTree tree)
    {
        var model = comp.GetSemanticModel(tree);
        var cpp = new StringBuilder();
        var tpl = new StringBuilder();
        foreach (var td in tree.GetRoot().DescendantNodes().OfType<BaseTypeDeclarationSyntax>())
        {
            var t = (INamedTypeSymbol)model.GetDeclaredSymbol(td);
            if (t.TypeKind == TypeKind.Enum || t.TypeKind == TypeKind.Interface) continue;
            var generic = Decls.IsGenericType(t);
            EmitType(t, model, generic ? tpl : cpp, tpl);
        }
        return (cpp.ToString(), tpl.ToString());
    }

    string Qual(INamedTypeSymbol t) => CN(t).Substring(2);
    string TplPrefix(INamedTypeSymbol t, IMethodSymbol m)
    {
        var s = decls.TemplateHeader(t);
        s += Decls.MethodTemplate(m);
        return s;
    }

    void EmitType(INamedTypeSymbol t, SemanticModel model, StringBuilder sb, StringBuilder tpl)
    {
        var q = Qual(t);
        var nameOnly = Names.Id(Names.FlatName(t));
        bool hasCinit = decls.HasStaticInit(t);
        // static field definitions
        foreach (var m in t.GetMembers())
        {
            string ty = null, nm = null;
            if (m is IFieldSymbol f && f.IsStatic && !f.IsConst && !f.IsImplicitlyDeclared) { ty = T(f.Type); nm = Names.Id(f.Name); }
            else if (m is IPropertySymbol p && p.IsStatic && Decls.IsFieldLike(p)) { ty = T(p.Type); nm = Names.Id(p.Name); }
            else if (m is IPropertySymbol p2 && p2.IsStatic && !Decls.IsFieldLike(p2) && Decls.IsAutoProp(p2)) { ty = T(p2.Type); nm = "__p_" + Names.Id(p2.Name); }
            else if (m is IEventSymbol e && e.IsStatic) { ty = T(e.Type); nm = Names.Id(e.Name); }
            if (ty != null) sb.AppendLine(decls.TemplateHeader(t) + ty + " " + q + "::" + nm + "{};");
        }
        if (hasCinit)
        {
            sb.AppendLine(decls.TemplateHeader(t) + "bool " + q + "::__cinit_done = false;");
            sb.AppendLine(decls.TemplateHeader(t) + "void " + q + "::__cinit_run() {");
            sb.AppendLine("  __cinit_done = true;");
            C = new Ctx { Type = t, Model = model, InStaticContext = true };
            foreach (var m in t.GetMembers())
            {
                EqualsValueClauseSyntax init = null; string target = null;
                if (m is IFieldSymbol f && f.IsStatic && !f.IsConst) { init = Decls.FieldInitializer(f); target = Names.Id(f.Name); }
                else if (m is IPropertySymbol p && p.IsStatic && Decls.IsFieldLike(p)) { init = Decls.PropInitializer(p); target = Names.Id(p.Name); }
                else if (m is IPropertySymbol p2 && p2.IsStatic && Decls.IsAutoProp(p2)) { init = Decls.PropInitializer(p2); target = "__p_" + Names.Id(p2.Name); }
                if (init == null) continue;
                var mm = ModelFor(init);
                C.Model = mm;
                var op = mm.GetOperation(init);
                var val = op is ISymbolInitializerOperation si ? si.Value : op;
                sb.AppendLine("  " + q + "::" + target + " = " + E(val) + ";");
            }
            foreach (var sc in t.StaticConstructors.Where(c => !c.IsImplicitlyDeclared))
            {
                var syn = (ConstructorDeclarationSyntax)sc.DeclaringSyntaxReferences[0].GetSyntax();
                var mm = ModelFor(syn);
                C = new Ctx { Type = t, Method = sc, Model = mm, InStaticContext = true };
                var body = mm.GetOperation(syn) as IConstructorBodyOperation;
                PrepareMethod(syn, body);
                sb.Append(Block(body.BlockBody, 1));
            }
            sb.AppendLine("}");
        }
        // constructors
        bool isClass = t.TypeKind == TypeKind.Class;
        string ctorRet = isClass ? CN(t) + "* " : "";
        string ctorName = isClass ? "__ctor" : nameOnly;
        foreach (var c in t.InstanceConstructors)
        {
            if (c.IsImplicitlyDeclared)
            {
                if (!isClass) continue;
                C = new Ctx { Type = t, Method = c, Model = model };
                sb.AppendLine(decls.TemplateHeader(t) + ctorRet + q + "::__ctor() {");
                if (hasCinit) sb.AppendLine("  __cinit();");
                sb.Append(FieldInits(t, 1));
                sb.Append(BaseCtorCall(t, null, 1));
                sb.AppendLine("  return this;");
                sb.AppendLine("}");
                continue;
            }
            if (!isClass && c.Parameters.Length == 0) continue;
            if (Decls.IsPrimaryCtor(c))
            {
                var tds = (TypeDeclarationSyntax)c.DeclaringSyntaxReferences[0].GetSyntax();
                var pm = ModelFor(tds);
                C = new Ctx { Type = t, Method = c, Model = pm };
                var pbase = tds.BaseList?.Types.OfType<PrimaryConstructorBaseTypeSyntax>().FirstOrDefault();
                IInvocationOperation binv = pbase != null ? Unwrap(pm.GetOperation(pbase)) as IInvocationOperation : null;
                sb.AppendLine(decls.TemplateHeader(t) + ctorRet + q + "::" + ctorName + "(" + ParamsDef(c) + ") {");
                if (hasCinit) sb.AppendLine("  __cinit();");
                foreach (var p in c.Parameters) sb.AppendLine("  this->__pc_" + Names.Id(p.Name) + " = " + Names.Id(p.Name) + ";");
                sb.Append(FieldInits(t, 1));
                if (isClass) { sb.Append(BaseCtorCall(t, binv, 1)); sb.AppendLine("  return this;"); }
                sb.AppendLine("}");
                continue;
            }
            var syn = (ConstructorDeclarationSyntax)c.DeclaringSyntaxReferences[0].GetSyntax();
            var mm = ModelFor(syn);
            C = new Ctx { Type = t, Method = c, Model = mm };
            var body = mm.GetOperation(syn) as IConstructorBodyOperation;
            PrepareMethod(syn, body);
            IInvocationOperation initInv = body?.Initializer != null ? Unwrap(body.Initializer) as IInvocationOperation : null;
            bool chainsThis = initInv != null && SymbolEqualityComparer.Default.Equals(initInv.TargetMethod.ContainingType.OriginalDefinition, t.OriginalDefinition);
            var head = decls.TemplateHeader(t) + ctorRet + q + "::" + ctorName + "(" + ParamsDef(c) + ")";
            if (!isClass && chainsThis) head += " : " + nameOnly + "(" + Args(initInv.TargetMethod, initInv.Arguments) + ")";
            sb.AppendLine(head + " {");
            if (hasCinit) sb.AppendLine("  __cinit();");
            if (isClass)
            {
                if (chainsThis) sb.AppendLine("  this->" + q + "::__ctor(" + Args(initInv.TargetMethod, initInv.Arguments) + ");");
                else { sb.Append(FieldInits(t, 1)); sb.Append(BaseCtorCall(t, initInv, 1)); }
            }
            else if (!chainsThis) sb.Append(FieldInits(t, 1));
            sb.Append(BoxedParamPrologue(c, 1));
            if (body?.BlockBody != null) sb.Append(Stmts(body.BlockBody.Operations, 1));
            else if (body?.ExpressionBody != null) sb.Append(Stmts(body.ExpressionBody.Operations, 1));
            if (isClass) sb.AppendLine("  return this;");
            sb.AppendLine("}");
        }
        // methods
        foreach (var m in t.GetMembers().OfType<IMethodSymbol>())
        {
            if (m.MethodKind is MethodKind.Constructor or MethodKind.StaticConstructor or MethodKind.Destructor) continue;
            if (m.IsImplicitlyDeclared || m.IsAbstract || m.IsExtern) continue;
            if (m.AssociatedSymbol is IEventSymbol) continue;
            var target = (m.IsGenericMethod || Decls.IsGenericType(t)) ? tpl : sb;
            if (m.AssociatedSymbol is IPropertySymbol ap)
            {
                if (Decls.IsFieldLike(ap)) continue;
                if (Decls.IsAutoProp(ap)) { EmitAutoAccessor(target, t, m, ap); continue; }
            }
            EmitMethod(target, t, m);
        }
    }

    // call to the base class' __ctor (C# order: field initializers, base ctor, body)
    string BaseCtorCall(INamedTypeSymbol t, IInvocationOperation inv, int ind)
    {
        var bt = t.BaseType;
        if (bt == null || !Names.IsOurs(bt))
        {
            if (inv != null && inv.Arguments.Length > 0 && bt != null && bt.SpecialType != SpecialType.System_Object)
                Warnings.Add("external base ctor args ignored: " + t.ToDisplayString());
            return "";
        }
        var args = inv != null ? Args(inv.TargetMethod, inv.Arguments) : "";
        return Ind(ind) + "this->" + CN(bt).Substring(2) + "::__ctor(" + args + ");\n";
    }

    void EmitAutoAccessor(StringBuilder sb, INamedTypeSymbol t, IMethodSymbol m, IPropertySymbol p)
    {
        var q = Qual(t);
        var fld = (p.IsStatic ? q + "::" : "this->") + "__p_" + Names.Id(p.Name);
        sb.AppendLine(TplPrefix(t, m) + T(m.ReturnType) + " " + q + "::" + Names.MemberName(m) + "(" + ParamsDef(m) + ") {");
        if (m.MethodKind == MethodKind.PropertyGet) sb.AppendLine("  return " + fld + ";");
        else sb.AppendLine("  " + fld + " = value;");
        sb.AppendLine("}");
    }

    SemanticModel ModelFor(SyntaxNode n) => comp.GetSemanticModel(n.SyntaxTree);

    string FieldInits(INamedTypeSymbol t, int ind)
    {
        var sb = new StringBuilder();
        var saved = C;
        foreach (var m in t.GetMembers())
        {
            EqualsValueClauseSyntax init = null; string target = null;
            if (m is IFieldSymbol f && !f.IsStatic && !f.IsConst) { init = Decls.FieldInitializer(f); target = Names.Id(f.Name); }
            else if (m is IPropertySymbol p && !p.IsStatic && Decls.IsFieldLike(p)) { init = Decls.PropInitializer(p); target = Names.Id(p.Name); }
            else if (m is IPropertySymbol p2 && !p2.IsStatic && Decls.IsAutoProp(p2)) { init = Decls.PropInitializer(p2); target = "__p_" + Names.Id(p2.Name); }
            else if (m is IEventSymbol e && !e.IsStatic) { init = Decls.EventInitializer(e); target = Names.Id(e.Name); }
            if (init == null) continue;
            var mm = ModelFor(init);
            C = new Ctx { Type = t, Model = mm, Method = saved?.Method };
            var op = mm.GetOperation(init);
            var val = op is ISymbolInitializerOperation si ? si.Value : op;
            sb.AppendLine(Ind(ind) + "this->" + target + " = " + E(val) + ";");
        }
        C = saved;
        return sb.ToString();
    }

    string ParamsDef(IMethodSymbol m) => string.Join(", ", m.Parameters.Select(p => Names.ParamType(p) + " " + ParamName(p)));
    string ParamName(IParameterSymbol p) => C != null && C.Boxed.Contains(p) ? Names.Id(p.Name) + "__p" : Names.Id(p.Name);

    string BoxedParamPrologue(IMethodSymbol m, int ind)
    {
        var sb = new StringBuilder();
        foreach (var p in m.Parameters)
            if (C.Boxed.Contains(p))
                sb.AppendLine(Ind(ind) + "auto* " + Names.Id(p.Name) + " = ::sys::gcnew<::sys::Ref<" + T(p.Type) + ">>(" + Names.Id(p.Name) + "__p);");
        return sb.ToString();
    }

    void EmitMethod(StringBuilder sb, INamedTypeSymbol t, IMethodSymbol m)
    {
        var syn = m.DeclaringSyntaxReferences.FirstOrDefault()?.GetSyntax();
        if (syn == null) return;
        var mm = ModelFor(syn);
        C = new Ctx { Type = t, Method = m, Model = mm, InStaticContext = m.IsStatic };
        IOperation bodyOp = null;
        if (syn is ArrowExpressionClauseSyntax arrow) bodyOp = mm.GetOperation(arrow);
        else bodyOp = mm.GetOperation(syn);
        IBlockOperation block = null;
        if (bodyOp is IMethodBodyOperation mb) block = mb.BlockBody ?? mb.ExpressionBody;
        else if (bodyOp is IBlockOperation b) block = b;
        if (block == null) { Warnings.Add("no body: " + m); return; }
        PrepareMethod(syn, block);
        var q = Qual(t);
        var name = Names.MemberName(m);
        var ret = T(m.ReturnType);
        bool hasCinit = decls.HasStaticInit(t) && m.IsStatic;
        if (m.IsIterator)
        {
            C.IsIterator = true;
            var gen = GenType(m);
            var argList = string.Join(", ", m.Parameters.Select(p => Names.Id(p.Name)));
            var targs = m.IsGenericMethod ? "<" + string.Join(", ", m.TypeParameters.Select(x => x.Name)) + ">" : "";
            var call = (m.IsStatic ? q + "::" : "this->") + (m.IsGenericMethod ? "template " : "") + name + "__it" + targs + "(" + argList + ")";
            var rt = (INamedTypeSymbol)m.ReturnType;
            // wrapper (params are never boxed in wrapper)
            var saveBoxed = C.Boxed; C.Boxed = new(SymbolEqualityComparer.Default);
            sb.AppendLine(TplPrefix(t, m) + ret + " " + q + "::" + name + "(" + ParamsDef(m) + ") {");
            C.Boxed = saveBoxed;
            if (hasCinit) sb.AppendLine("  __cinit();");
            if (rt.Name == "IEnumerator")
                sb.AppendLine("  return ::sys::MakeEnumerator(" + call + ");");
            else
            {
                var cap = m.IsStatic ? "[=]" : "[=, this]";
                sb.AppendLine("  return ::sys::MakeEnumerable(::sys::fn<" + gen + "()>(" + cap + "() { return " + call + "; }));");
            }
            sb.AppendLine("}");
            sb.AppendLine(TplPrefix(t, m) + gen + " " + q + "::" + name + "__it(" + ParamsDef(m) + ") {");
            sb.Append(BoxedParamPrologue(m, 1));
            sb.Append(Stmts(block.Operations, 1));
            sb.AppendLine("}");
            return;
        }
        if (m.ReturnsByRef) ret += "&";
        sb.AppendLine(TplPrefix(t, m) + ret + " " + q + "::" + name + "(" + ParamsDef(m) + ") {");
        if (hasCinit) sb.AppendLine("  __cinit();");
        sb.Append(BoxedParamPrologue(m, 1));
        sb.Append(Stmts(block.Operations, 1));
        sb.AppendLine("}");
    }

    // Find captured-and-written variables, goto targets
    void PrepareMethod(SyntaxNode syn, IOperation body)
    {
        if (body == null) return;
        var captured = new HashSet<ISymbol>(SymbolEqualityComparer.Default);
        foreach (var lam in body.Descendants().OfType<IAnonymousFunctionOperation>())
        {
            try
            {
                var df = C.Model.AnalyzeDataFlow(lam.Syntax);
                foreach (var s in df.Captured) captured.Add(s);
            }
            catch { }
        }
        // also: variables captured by an iterator-local lambda are fine (frames are heap)
        foreach (var op in body.DescendantsAndSelf())
        {
            ISymbol written = null;
            IOperation target = op switch
            {
                ISimpleAssignmentOperation a => a.Target,
                ICompoundAssignmentOperation ca => ca.Target,
                IIncrementOrDecrementOperation inc => inc.Target,
                ICoalesceAssignmentOperation co => co.Target,
                IArgumentOperation arg when arg.Parameter != null && arg.Parameter.RefKind != RefKind.None => arg.Value,
                _ => null
            };
            if (target is ILocalReferenceOperation lr) written = lr.Local;
            else if (target is IParameterReferenceOperation pr) written = pr.Parameter;
            if (written != null && captured.Contains(written)) C.Boxed.Add(written);
        }
        foreach (var br in body.Descendants().OfType<IBranchOperation>())
            if (br.BranchKind == BranchKind.GoTo) C.GotoTargets.Add(br.Target);
    }

    bool IsInCtorBody(IOperation op)
    {
        for (var p = op.Parent; p != null; p = p.Parent)
            if (p is IAnonymousFunctionOperation) return false;
        return true;
    }

    static IOperation Unwrap(IOperation op)
    {
        while (op is IExpressionStatementOperation es) op = es.Operation;
        return op;
    }

    static string Ind(int n) => new string(' ', n * 2);

    // ===================== statements =====================
    string Block(IBlockOperation b, int ind) => Ind(ind) + "{\n" + Stmts(b.Operations, ind + 1) + Ind(ind) + "}\n";

    string Stmts(IEnumerable<IOperation> ops, int ind)
    {
        var sb = new StringBuilder();
        foreach (var o in ops) sb.Append(S(o, ind));
        return sb.ToString();
    }

    string Body(IOperation op, int ind)
    {
        if (op is IBlockOperation b) return Block(b, ind);
        return Ind(ind) + "{\n" + S(op, ind + 1) + Ind(ind) + "}\n";
    }

    // Collect out-var / pattern declared locals that belong directly to this statement
    List<ILocalSymbol> DeclaredInExpr(IOperation op)
    {
        var res = new List<ILocalSymbol>();
        void Walk(IOperation o)
        {
            if (o == null) return;
            if (o is IAnonymousFunctionOperation) return;
            if (o is IDeclarationExpressionOperation de)
            {
                if (de.Expression is ILocalReferenceOperation lr) res.Add(lr.Local);
            }
            if (o is IDeclarationPatternOperation dp && dp.DeclaredSymbol is ILocalSymbol ls) res.Add(ls);
            if (o is IRecursivePatternOperation rp && rp.DeclaredSymbol is ILocalSymbol rls) res.Add(rls);
            foreach (var c in o.ChildOperations) Walk(c);
        }
        Walk(op);
        return res;
    }

    string DeclareLocal(ILocalSymbol l, string init = null)
    {
        var n = Names.Id(l.Name);
        if (C.Predeclared.Contains(l))
            return init == null ? ";" : LocalRef(l) + " = " + init + ";";
        if (l.RefKind != RefKind.None && init != null)
            return T(l.Type) + "& " + n + " = " + init + ";";
        if (C.Boxed.Contains(l))
            return "auto* " + n + " = ::sys::gcnew<::sys::Ref<" + T(l.Type) + ">>(" + (init ?? "") + ");";
        if (init == null) return T(l.Type) + " " + n + "{};";
        return T(l.Type) + " " + n + " = " + init + ";";
    }

    string Hoist(IEnumerable<IOperation> exprs, int ind)
    {
        var sb = new StringBuilder();
        foreach (var e in exprs)
            foreach (var l in DeclaredInExpr(e)) sb.AppendLine(Ind(ind) + DeclareLocal(l));
        return sb.ToString();
    }

    string S(IOperation op, int ind)
    {
        var I = Ind(ind);
        switch (op)
        {
            case IBlockOperation b:
                return Block(b, ind);
            case IVariableDeclarationGroupOperation g:
            {
                var sb = new StringBuilder();
                sb.Append(Hoist(new[] { g }, ind));
                foreach (var d in g.Declarations)
                    foreach (var v in d.Declarators)
                    {
                        var init = v.Initializer?.Value ?? d.Initializer?.Value;
                        sb.AppendLine(I + DeclareLocal(v.Symbol, init != null ? E(init) : null));
                    }
                return sb.ToString();
            }
            case IExpressionStatementOperation es:
                return Hoist(new[] { es.Operation }, ind) + I + ES(es.Operation) + ";\n";
            case IConditionalOperation c:
            {
                var decl = Hoist(new[] { c.Condition }, ind);
                var s = I + "if (" + E(c.Condition) + ")\n" + Body(c.WhenTrue, ind);
                if (c.WhenFalse != null)
                {
                    if (c.WhenFalse is IConditionalOperation && DeclaredInExpr(((IConditionalOperation)c.WhenFalse).Condition).Count == 0)
                        s += I + "else " + S(c.WhenFalse, ind).TrimStart();
                    else s += I + "else\n" + Body(c.WhenFalse, ind);
                }
                return decl + s;
            }
            case IWhileLoopOperation w:
            {
                var decl = Hoist(new[] { w.Condition }, ind + 1);
                string s;
                if (w.ConditionIsTop) s = I + "while (" + (w.Condition == null ? "true" : E(w.Condition)) + ")\n" + Body(w.Body, ind);
                else s = I + "do\n" + Body(w.Body, ind) + I + "while (" + E(w.Condition) + ");\n";
                if (decl.Length > 0) return I + "{\n" + decl + Indent(s) + I + "}\n";
                return s;
            }
            case IForLoopOperation f:
            {
                var sb = new StringBuilder();
                sb.AppendLine(I + "{");
                foreach (var b in f.Before) sb.Append(S(b, ind + 1));
                var cond = f.Condition == null ? "" : E(f.Condition);
                var bottom = string.Join(", ", f.AtLoopBottom.Select(x => ES(Unwrap(x))));
                sb.AppendLine(Ind(ind + 1) + "for (; " + cond + "; " + bottom + ")");
                sb.Append(Body(f.Body, ind + 1));
                sb.AppendLine(I + "}");
                return sb.ToString();
            }
            case IForEachLoopOperation fe:
                return ForEach(fe, ind);
            case IBranchOperation br:
            {
                if (br.BranchKind == BranchKind.Break)
                {
                    foreach (var (exit, gotoName) in C.SwitchExits)
                        if (SymbolEqualityComparer.Default.Equals(exit, br.Target) && gotoName != null) return I + "goto " + gotoName + ";\n";
                    return I + "break;\n";
                }
                if (br.BranchKind == BranchKind.Continue) return I + "continue;\n";
                return I + "goto " + LabelName(br.Target) + ";\n";
            }
            case ILabeledOperation lab:
                return I + LabelName(lab.Label) + ":;\n" + (lab.Operation != null ? S(lab.Operation, ind) : "");
            case IReturnOperation r:
            {
                var decl = r.ReturnedValue != null ? Hoist(new[] { r.ReturnedValue }, ind) : "";
                if (r.Kind == OperationKind.YieldBreak) return I + "co_return;\n";
                if (r.Kind == OperationKind.YieldReturn) return decl + I + "co_yield " + E(r.ReturnedValue) + ";\n";
                if (r.ReturnedValue == null) return I + (C.Method != null && C.Method.MethodKind == MethodKind.Constructor && C.Type.TypeKind == TypeKind.Class && !(r.Parent is null) && IsInCtorBody(r) ? "return this;\n" : "return;\n");
                return decl + I + "return " + E(r.ReturnedValue) + ";\n";
            }
            case ISwitchOperation sw:
                return Switch(sw, ind);
            case ITryOperation tr:
                return Try(tr, ind);
            case IThrowOperation th:
                return I + (th.Exception == null ? "throw;" : "throw " + E(th.Exception) + ";") + "\n";
            case IUsingOperation us:
            {
                var sb = new StringBuilder();
                sb.AppendLine(I + "{");
                string resVar;
                if (us.Resources is IVariableDeclarationGroupOperation g)
                {
                    sb.Append(S(g, ind + 1));
                    resVar = Names.Id(g.Declarations[0].Declarators[0].Symbol.Name);
                }
                else
                {
                    resVar = Tmp("__using");
                    sb.AppendLine(Ind(ind + 1) + "auto " + resVar + " = " + E(us.Resources) + ";");
                }
                var resType = us.Resources is IVariableDeclarationGroupOperation g2 ? g2.Declarations[0].Declarators[0].Symbol.Type : us.Resources.Type;
                if (resType != null && resType.IsValueType)
                    sb.AppendLine(Ind(ind + 1) + "auto " + Tmp("__fin") + " = ::sys::finally([&]{ " + resVar + ".Dispose(); });");
                else
                    sb.AppendLine(Ind(ind + 1) + "auto " + Tmp("__fin") + " = ::sys::finally([&]{ if (" + resVar + ") " + resVar + "->Dispose(); });");
                sb.Append(Body(us.Body, ind + 1));
                sb.AppendLine(I + "}");
                return sb.ToString();
            }
            case IUsingDeclarationOperation ud:
            {
                var sb = new StringBuilder();
                sb.Append(S(ud.DeclarationGroup, ind));
                foreach (var d in ud.DeclarationGroup.Declarations)
                    foreach (var v in d.Declarators)
                    {
                        var n = LocalRef(v.Symbol);
                        sb.AppendLine(I + "auto " + Tmp("__fin") + " = ::sys::finally([&]{ if (" + n + ") " + n + "->Dispose(); });");
                    }
                return sb.ToString();
            }
            case ILockOperation lk:
                return Body(lk.Body, ind);
            case IEmptyOperation:
                return I + ";\n";
            case ILocalFunctionOperation lf:
                Warnings.Add("local function " + lf.Symbol);
                return I + "/* local function " + lf.Symbol.Name + " */\n";
            default:
                if (op.Type != null || op is IInvocationOperation) return I + ES(op) + ";\n";
                Warnings.Add("unhandled stmt " + op.Kind + " at " + op.Syntax.GetLocation().GetLineSpan());
                return I + "/* unhandled stmt " + op.Kind + " */\n";
        }
    }

    static string Indent(string s) => string.Join("\n", s.TrimEnd('\n').Split('\n').Select(l => "  " + l)) + "\n";

    string LabelName(ILabelSymbol l)
    {
        if (!C.Labels.TryGetValue(l, out var n))
        {
            n = "__L" + C.Labels.Count + "_" + Names.Id(string.IsNullOrEmpty(l.Name) ? "x" : new string(l.Name.Select(ch => char.IsLetterOrDigit(ch) ? ch : '_').ToArray()));
            C.Labels[l] = n;
        }
        return n;
    }

    string ForEach(IForEachLoopOperation fe, int ind)
    {
        var I = Ind(ind);
        var coll = fe.Collection;
        while (coll is IConversionOperation cv && cv.IsImplicit) coll = cv.Operand;
        var sb = new StringBuilder();
        var e = Tmp("__e");
        ILocalSymbol loc = fe.Locals.FirstOrDefault();
        if (fe.LoopControlVariable is IVariableDeclaratorOperation vd) loc = vd.Symbol;
        ITypeSymbol et = null;
        if (fe.Syntax is ForEachStatementSyntax fes) et = C.Model.GetForEachStatementInfo(fes).ElementType;
        string elemConv = e;
        if (loc != null)
        {
            if (et != null && !SymbolEqualityComparer.Default.Equals(et, loc.Type))
            {
                if (Names.IsPointerLike(loc.Type) && Names.IsPointerLike(et))
                {
                    var conv = comp.ClassifyCommonConversion(et, loc.Type);
                    elemConv = conv.IsImplicit ? e : "::sys::cast<" + T(loc.Type) + ">(" + e + ")";
                }
                else if (et.SpecialType == SpecialType.System_Object && !Names.IsPointerLike(loc.Type)) elemConv = "::sys::unbox<" + T(loc.Type) + ">(" + e + ")";
                else elemConv = "static_cast<" + T(loc.Type) + ">(" + e + ")";
            }
        }
        sb.AppendLine(I + "for (auto " + e + " : ::sys::each(" + E(coll) + ")) {");
        if (loc != null) sb.AppendLine(Ind(ind + 1) + DeclareLocal(loc, elemConv));
        if (fe.Body is IBlockOperation b) sb.Append(Stmts(b.Operations, ind + 1));
        else sb.Append(S(fe.Body, ind + 1));
        sb.AppendLine(I + "}");
        return sb.ToString();
    }

    bool IsNativeSwitchType(ITypeSymbol t)
    {
        if (t.TypeKind == TypeKind.Enum) return true;
        switch (t.SpecialType)
        {
            case SpecialType.System_Int32: case SpecialType.System_Int16: case SpecialType.System_Int64: case SpecialType.System_Byte:
            case SpecialType.System_Char: case SpecialType.System_UInt32: case SpecialType.System_UInt16: case SpecialType.System_SByte:
            case SpecialType.System_Boolean: case SpecialType.System_UInt64:
                return true;
        }
        return false;
    }

    string Switch(ISwitchOperation sw, int ind)
    {
        var I = Ind(ind);
        var sb = new StringBuilder();
        bool native = IsNativeSwitchType(sw.Value.Type) && sw.Cases.All(c => c.Clauses.All(cl => cl is IDefaultCaseClauseOperation || (cl is ISingleValueCaseClauseOperation sv && sv.Value.ConstantValue.HasValue)));
        var decl = Hoist(new[] { sw.Value }, ind + 1);
        if (decl.Length > 0) sb.AppendLine(I + "{").Append(decl);
        if (native)
        {
            C.SwitchExits.Push((sw.ExitLabel, null));
            foreach (var cs in sw.Cases)
                foreach (var l in cs.Locals)
                    if (!C.Predeclared.Contains(l)) { sb.AppendLine(I + DeclareLocal(l)); C.Predeclared.Add(l); }
            sb.AppendLine(I + "switch (" + E(sw.Value) + ") {");
            foreach (var cs in sw.Cases)
            {
                foreach (var cl in cs.Clauses)
                {
                    if (cl is IDefaultCaseClauseOperation) sb.AppendLine(I + "default:");
                    else sb.AppendLine(I + "case " + E(((ISingleValueCaseClauseOperation)cl).Value) + ":");
                    if (cl.Label != null && C.GotoTargets.Contains(cl.Label)) sb.AppendLine(I + LabelName(cl.Label) + ":;");
                }
                sb.AppendLine(I + "{");
                sb.Append(Stmts(cs.Body, ind + 1));
                sb.AppendLine(I + "}");
            }
            sb.AppendLine(I + "}");
            C.SwitchExits.Pop();
        }
        else
        {
            var v = Tmp("__sw");
            var end = Tmp("__swend");
            C.SwitchExits.Push((sw.ExitLabel, end));
            sb.AppendLine(I + "{");
            sb.AppendLine(Ind(ind + 1) + "auto " + v + " = " + E(sw.Value) + ";");
            // pattern locals
            foreach (var cs in sw.Cases)
                foreach (var cl in cs.Clauses)
                    if (cl is IPatternCaseClauseOperation pc)
                        foreach (var l in DeclaredInExpr(pc.Pattern)) sb.AppendLine(Ind(ind + 1) + DeclareLocal(l));
            ISwitchCaseOperation def = null;
            bool first = true;
            foreach (var cs in sw.Cases)
            {
                if (cs.Clauses.Any(cl => cl is IDefaultCaseClauseOperation)) { def = cs; continue; }
                var conds = new List<string>();
                foreach (var cl in cs.Clauses)
                {
                    if (cl is ISingleValueCaseClauseOperation sv) conds.Add(EqualsExpr(v, sw.Value.Type, sv.Value));
                    else if (cl is IPatternCaseClauseOperation pc)
                    {
                        var c = PatternTest(v, sw.Value.Type, pc.Pattern);
                        if (pc.Guard != null) c = "(" + c + " && " + E(pc.Guard) + ")";
                        conds.Add(c);
                    }
                    else conds.Add("false /*clause " + cl.CaseKind + "*/");
                }
                sb.AppendLine(Ind(ind + 1) + (first ? "if (" : "else if (") + string.Join(" || ", conds) + ") {");
                sb.Append(Stmts(cs.Body, ind + 2));
                sb.AppendLine(Ind(ind + 1) + "}");
                first = false;
            }
            if (def != null)
            {
                sb.AppendLine(Ind(ind + 1) + (first ? "{" : "else {"));
                sb.Append(Stmts(def.Body, ind + 2));
                sb.AppendLine(Ind(ind + 1) + "}");
            }
            sb.AppendLine(Ind(ind + 1) + end + ":;");
            sb.AppendLine(I + "}");
            C.SwitchExits.Pop();
        }
        if (decl.Length > 0) sb.AppendLine(I + "}");
        return sb.ToString();
    }

    string EqualsExpr(string v, ITypeSymbol vt, IOperation value)
    {
        if (vt.SpecialType == SpecialType.System_String) return "::System::String::op_Equality(" + v + ", " + E(value) + ")";
        if (value.ConstantValue.HasValue && value.ConstantValue.Value == null) return "(" + v + " == nullptr)";
        return "(" + v + " == " + E(value) + ")";
    }

    string PatternTest(string v, ITypeSymbol vt, IPatternOperation p)
    {
        switch (p)
        {
            case IConstantPatternOperation cp:
                return EqualsExpr(v, vt, cp.Value);
            case IDeclarationPatternOperation dp:
            {
                var mt = dp.MatchedType ?? dp.NarrowedType;
                if (dp.DeclaredSymbol is ILocalSymbol ls)
                    return "::sys::is_as<" + T(mt) + ">(" + v + ", " + LocalRef(ls) + ")";
                return "::sys::is<" + T(mt) + ">(" + v + ")";
            }
            case ITypePatternOperation tp:
                return "::sys::is<" + T(tp.MatchedType) + ">(" + v + ")";
            case INegatedPatternOperation np:
                return "!(" + PatternTest(v, vt, np.Pattern) + ")";
            case IBinaryPatternOperation bp:
                return "(" + PatternTest(v, vt, bp.LeftPattern) + (bp.OperatorKind == BinaryOperatorKind.Or ? " || " : " && ") + PatternTest(v, vt, bp.RightPattern) + ")";
            case IRecursivePatternOperation rp:
            {
                var parts = new List<string>();
                var mt = rp.MatchedType;
                string tv = v;
                if (mt != null && !SymbolEqualityComparer.Default.Equals(mt, vt) && Names.IsPointerLike(mt))
                {
                    parts.Add("::sys::is<" + T(mt) + ">(" + v + ")");
                    tv = "::sys::cast<" + T(mt) + ">(" + v + ")";
                }
                else if (vt != null && Names.IsPointerLike(vt)) parts.Add("(" + v + " != nullptr)");
                if (rp.DeclaredSymbol is ILocalSymbol dl) parts.Add("((" + LocalRef(dl) + " = " + tv + "), true)");
                foreach (var pp in rp.PropertySubpatterns)
                {
                    string mv;
                    if (pp.Member is IPropertyReferenceOperation prr)
                        mv = Decls.IsFieldLike(prr.Property) ? Paren(tv) + (Names.IsPointerLike(mt ?? vt) ? "->" : ".") + Names.Id(prr.Property.Name)
                            : Paren(tv) + (Names.IsPointerLike(mt ?? vt) ? "->" : ".") + "get_" + prr.Property.Name + "()";
                    else if (pp.Member is IFieldReferenceOperation frr) mv = Paren(tv) + (Names.IsPointerLike(mt ?? vt) ? "->" : ".") + Names.Id(frr.Field.Name);
                    else { mv = "/*member*/"; Warnings.Add("recursive pattern member " + pp.Member.Kind); }
                    parts.Add(PatternTest(mv, pp.Member.Type, pp.Pattern));
                }
                return parts.Count == 0 ? "true" : "(" + string.Join(" && ", parts) + ")";
            }
            case IDiscardPatternOperation:
                return "true";
            case IRelationalPatternOperation rp:
                return "(" + v + " " + BinOp(rp.OperatorKind) + " " + E(rp.Value) + ")";
        }
        Warnings.Add("unhandled pattern " + p.Kind);
        return "false /*pattern*/";
    }

    string Try(ITryOperation tr, int ind)
    {
        var I = Ind(ind);
        var sb = new StringBuilder();
        sb.AppendLine(I + "{");
        if (tr.Finally != null)
        {
            sb.AppendLine(Ind(ind + 1) + "auto " + Tmp("__fin") + " = ::sys::finally([&]() {");
            sb.Append(Stmts(tr.Finally.Operations, ind + 2));
            sb.AppendLine(Ind(ind + 1) + "});");
        }
        if (tr.Catches.Length > 0)
        {
            sb.AppendLine(Ind(ind + 1) + "try");
            sb.Append(Block(tr.Body, ind + 1));
            foreach (var c in tr.Catches)
            {
                string decl = "...";
                string bind = null;
                var ext = c.ExceptionType;
                if (c.ExceptionDeclarationOrExpression is IVariableDeclaratorOperation vd)
                {
                    decl = T(ext) + " " + Names.Id(vd.Symbol.Name);
                }
                else if (ext != null && ext.Name != "Object") decl = T(ext) + " __ex";
                sb.AppendLine(Ind(ind + 1) + "catch (" + decl + ")");
                sb.Append(Block(c.Handler, ind + 1));
            }
        }
        else sb.Append(Block(tr.Body, ind + 1));
        sb.AppendLine(I + "}");
        return sb.ToString();
    }
}
