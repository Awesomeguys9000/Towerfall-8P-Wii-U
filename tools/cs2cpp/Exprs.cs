using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Microsoft.CodeAnalysis.Operations;
using System.Text;

partial class Bodies
{
    // Statement-form expression (value unused)
    string ES(IOperation op)
    {
        switch (op)
        {
            case ISimpleAssignmentOperation a: return Assign(a, true);
            case ICompoundAssignmentOperation ca: return Compound(ca, true);
            case IIncrementOrDecrementOperation inc: return IncDec(inc, true);
            case IEventAssignmentOperation ea: return EventAssign(ea);
            case IConditionalAccessOperation cao when cao.Type == null || cao.Type.SpecialType == SpecialType.System_Void || true:
                return CondAccess(cao, true);
        }
        return E(op);
    }

    bool IsStatementContext(IOperation op) => op.Parent is IExpressionStatementOperation || (op.Parent is IForLoopOperation);

    static bool IsSimple(string s) => s.All(ch => char.IsLetterOrDigit(ch) || ch == '_' || ch == ':' ) || s == "this";

    public string E(IOperation op)
    {
        if (op == null) return "/*null*/";
        if (op.ConstantValue.HasValue && op.Type != null && !(op is IConversionOperation co0 && (co0.GetConversion().IsBoxing || co0.GetConversion().IsNullable)))
            return Const(op.ConstantValue.Value, op.Type);
        switch (op)
        {
            case ILiteralOperation l: return Const(l.ConstantValue.Value, l.Type);
            case ILocalReferenceOperation lr: return LocalRef(lr.Local);
            case IParameterReferenceOperation pr when pr.Parameter.ContainingSymbol is IMethodSymbol pcm && Decls.IsPrimaryCtor(pcm) && !SymbolEqualityComparer.Default.Equals(C.Method, pcm):
                return "this->__pc_" + Names.Id(pr.Parameter.Name);
            case IParameterReferenceOperation pr: return C.Boxed.Contains(pr.Parameter) ? Names.Id(pr.Parameter.Name) + "->v" : Names.Id(pr.Parameter.Name);
            case IInstanceReferenceOperation ir:
                if (ir.ReferenceKind == InstanceReferenceKind.ImplicitReceiver) return C.ImplicitReceivers.Peek();
                if (C.Type.IsValueType) return "(*this)";
                return "this";
            case IFieldReferenceOperation fr: return FieldRef(fr);
            case IPropertyReferenceOperation pr: return PropGet(pr);
            case IEventReferenceOperation er: return Recv(er.Instance, er.Event) + Names.Id(er.Event.Name);
            case IArrayElementReferenceOperation ae:
            {
                var arr = E(ae.ArrayReference);
                if (ae.Indices.Length == 1) return "(*::sys::nn(" + arr + "))[" + E(ae.Indices[0]) + "]";
                return "::sys::nn(" + arr + ")->at(" + string.Join(", ", ae.Indices.Select(E)) + ")";
            }
            case IInvocationOperation inv: return Invoke(inv);
            case IObjectCreationOperation oc: return Create(oc);
            case ITypeParameterObjectCreationOperation tpc: return "::sys::New<" + T(tpc.Type) + ">()";
            case IArrayCreationOperation ac: return ArrayCreate(ac);
            case IDelegateCreationOperation dc: return DelegateCreate(dc);
            case IAnonymousFunctionOperation af: return Lambda(af, (INamedTypeSymbol)null);
            case IConversionOperation cv: return Conv(cv);
            case IBinaryOperation b: return Binary(b);
            case IUnaryOperation u: return Unary(u);
            case ISimpleAssignmentOperation a: return Assign(a, IsStatementContext(a));
            case ICompoundAssignmentOperation ca: return Compound(ca, IsStatementContext(ca));
            case IIncrementOrDecrementOperation inc: return IncDec(inc, IsStatementContext(inc));
            case IEventAssignmentOperation ea: return EventAssign(ea);
            case ICoalesceOperation co:
            {
                var t = Tmp("__co");
                bool nv = co.Value.Type != null && co.Value.Type.OriginalDefinition.SpecialType == SpecialType.System_Nullable_T;
                bool nr = co.Type != null && co.Type.OriginalDefinition.SpecialType == SpecialType.System_Nullable_T;
                var test = nv ? t + ".get_HasValue()" : t + " != nullptr";
                var val = nv && !nr ? t + ".get_Value()" : t;
                return "([&]() -> " + T(co.Type) + " { auto " + t + " = " + E(co.Value) + "; if (" + test + ") return " + val + "; return " + E(co.WhenNull) + "; }())";
            }
            case IConditionalAccessOperation cao: return CondAccess(cao, false);
            case IConditionalAccessInstanceOperation: return C.CondAccessVar;
            case ISwitchExpressionOperation se:
            {
                var v = Tmp("__sx");
                var sb = new StringBuilder();
                sb.Append("([&]() -> " + T(se.Type) + " { auto " + v + " = " + E(se.Value) + "; ");
                foreach (var arm in se.Arms)
                    foreach (var l in DeclaredInExpr(arm.Pattern)) sb.Append(DeclareLocal(l) + " ");
                foreach (var arm in se.Arms)
                {
                    var c = PatternTest(v, se.Value.Type, arm.Pattern);
                    if (arm.Guard != null) c = "(" + c + " && " + E(arm.Guard) + ")";
                    var val = E(arm.Value);
                    if (Names.IsPointerLike(se.Type) && arm.Value.Type != null && !SymbolEqualityComparer.Default.Equals(arm.Value.Type, se.Type)) val = "static_cast<" + T(se.Type) + ">(" + val + ")";
                    sb.Append("if (" + c + ") return " + val + "; ");
                }
                sb.Append("::sys::throw_switch(); }())");
                return sb.ToString();
            }
            case IConditionalOperation c:
            {
                var tt = c.Type;
                string a = E(c.WhenTrue), b = E(c.WhenFalse);
                if (tt != null && Names.IsPointerLike(tt))
                {
                    if (!SymbolEqualityComparer.Default.Equals(c.WhenTrue.Type, tt)) a = "static_cast<" + T(tt) + ">(" + a + ")";
                    if (!SymbolEqualityComparer.Default.Equals(c.WhenFalse.Type, tt)) b = "static_cast<" + T(tt) + ">(" + b + ")";
                }
                return "(" + E(c.Condition) + " ? " + a + " : " + b + ")";
            }
            case IIsTypeOperation it:
                return (it.IsNegated ? "!" : "") + "::sys::is<" + T(it.TypeOperand) + ">(" + E(it.ValueOperand) + ")";
            case IIsPatternOperation ip:
            {
                var ve = E(ip.Value);
                if (IsSimple(ve) || ip.Pattern is IConstantPatternOperation || ip.Pattern is ITypePatternOperation) return PatternTest(ve, ip.Value.Type, ip.Pattern);
                var pv = Tmp("__pv");
                return "([&](auto " + pv + ") -> bool { return " + PatternTest(pv, ip.Value.Type, ip.Pattern) + "; }(" + ve + "))";
            }
            case ITypeOfOperation to: return "::sys::typeof_<" + T(to.TypeOperand) + ">()";
            case IDefaultValueOperation dv: return Default(dv.Type);
            case IDeclarationExpressionOperation de: return E(de.Expression);
            case IDiscardOperation dis: return "::sys::discard<" + T(dis.Type) + ">()";
            case IThrowOperation th: return "::sys::throw_any(" + E(th.Exception) + ")";
            case IParenthesizedOperation p: return "(" + E(p.Operand) + ")";
            case ISizeOfOperation so: return "(int32_t)sizeof(" + T(so.TypeOperand) + ")";
            case IObjectOrCollectionInitializerOperation: break;
            case IArrayInitializerOperation ai: return "{" + string.Join(", ", ai.ElementValues.Select(E)) + "}";
            case INameOfOperation no: return Const(no.ConstantValue.Value, no.Type);
            case IInterpolatedStringOperation iso:
                return "::sys::concat_all(" + string.Join(", ", iso.Parts.Select(pt => pt is IInterpolatedStringTextOperation tx ? E(tx.Text) : E(((IInterpolationOperation)pt).Expression))) + ")";
        }
        Warnings.Add("unhandled expr " + op.Kind + " at " + op.Syntax.GetLocation().GetLineSpan());
        return "/*unhandled " + op.Kind + "*/";
    }

    string Paren(string s) => IsSimple(s) ? s : "(" + s + ")";

    string LocalRef(ILocalSymbol l) => C.Boxed.Contains(l) ? Names.Id(l.Name) + "->v" : Names.Id(l.Name);

    string Default(ITypeSymbol t)
    {
        if (t is ITypeParameterSymbol) return T(t) + "{}";
        if (t.IsReferenceType) return "(" + T(t) + ")nullptr";
        return T(t) + "{}";
    }

    public string Const(object v, ITypeSymbol t)
    {
        if (v == null)
        {
            if (t != null && t.IsValueType) return T(t) + "{}";
            if (t != null && t.SpecialType != SpecialType.System_Object && t.TypeKind != TypeKind.Error) return "(" + T(t) + ")nullptr";
            return "nullptr";
        }
        if (t.TypeKind == TypeKind.Enum)
        {
            var et = (INamedTypeSymbol)t;
            var match = et.GetMembers().OfType<IFieldSymbol>().Where(f => f.IsConst && Equals(f.ConstantValue, v)).ToList();
            if (match.Count >= 1) return CN(t) + "::" + Names.Id(match[0].Name);
            return "static_cast<" + CN(t) + ">(" + Lit.Int(v, et.EnumUnderlyingType) + ")";
        }
        switch (t.SpecialType)
        {
            case SpecialType.System_Boolean: return (bool)v ? "true" : "false";
            case SpecialType.System_String: return "TF_S(u\"" + Names.Escape16((string)v) + "\")";
            case SpecialType.System_Char: return "(char16_t)" + (int)(char)v;
            case SpecialType.System_Single: return Lit.Float(Convert.ToSingle(v));
            case SpecialType.System_Double: return Lit.Double(Convert.ToDouble(v));
            case SpecialType.System_Decimal: return Lit.Double(Convert.ToDouble(v));
            case SpecialType.System_Int32: return Lit.Int(Convert.ToInt32(v), t);
            case SpecialType.System_UInt32: return Lit.Int(Convert.ToUInt32(v), t);
            case SpecialType.System_Int64: return Lit.Int(Convert.ToInt64(v), t);
            case SpecialType.System_UInt64: return Lit.Int(Convert.ToUInt64(v), t);
            case SpecialType.System_Int16: return "(int16_t)" + Lit.Int(Convert.ToInt16(v), t);
            case SpecialType.System_UInt16: return "(uint16_t)" + Lit.Int(Convert.ToUInt16(v), t);
            case SpecialType.System_Byte: return "(uint8_t)" + Lit.Int(Convert.ToByte(v), t);
            case SpecialType.System_SByte: return "(int8_t)" + Lit.Int(Convert.ToSByte(v), t);
            case SpecialType.System_Object:
                // constant boxed? shouldn't happen
                break;
        }
        if (v is string s) return "TF_S(u\"" + Names.Escape16(s) + "\")";
        if (v is bool bb) return bb ? "true" : "false";
        if (v is float ff) return Lit.Float(ff);
        if (v is double dd) return Lit.Double(dd);
        if (v is int ii) return Lit.Int(ii, t);
        return v.ToString();
    }

    string Owner(ITypeSymbol ct) => Names.IsPrimitive(ct) ? "::System::" + ct.MetadataName : CN(ct);

    // receiver prefix for a member access
    string Recv(IOperation instance, ISymbol member)
    {
        if (instance == null || member.IsStatic)
        {
            var ct = member.ContainingType;
            return Owner(ct) + "::";
        }
        if (instance is IInstanceReferenceOperation ir && ir.ReferenceKind != InstanceReferenceKind.ImplicitReceiver)
        {
            if (instance.Syntax is BaseExpressionSyntax)
                return CN(C.Type.BaseType) + "::";
            return "this->";
        }
        var e = E(instance);
        var t = instance.Type;
        if (t != null && Names.IsPointerLike(t)) return "::sys::nn(" + e + ")->";
        if (t is ITypeParameterSymbol) return "::sys::nn(" + e + ")->";
        return Paren(e) + ".";
    }

    bool NeedsGuard(INamedTypeSymbol owner)
    {
        if (owner == null || !Names.IsOurs(owner)) return false;
        if (!decls.HasStaticInit(owner.OriginalDefinition)) return false;
        for (var t = C.Type; t != null; t = t.ContainingType)
            if (SymbolEqualityComparer.Default.Equals(t.OriginalDefinition, owner.OriginalDefinition)) return false;
        return true;
    }

    string FieldRef(IFieldReferenceOperation fr)
    {
        var f = fr.Field;
        if (f.IsStatic)
        {
            var s = CN(f.ContainingType) + "::" + Names.Id(f.Name);
            if (NeedsGuard(f.ContainingType)) return "(" + CN(f.ContainingType) + "::__cinit(), " + s + ")";
            return s;
        }
        if (!Names.IsOurs(f)) NoteExternal(f);
        return Recv(fr.Instance, f) + Names.Id(f.Name);
    }

    void NoteExternal(ISymbol s)
    {
        ExternalUses.Add(s.OriginalDefinition.ToDisplayString() + "  [" + s.Kind + (s.IsStatic ? " static" : "") + "]");
    }

    string ArgList(IEnumerable<IArgumentOperation> args) => string.Join(", ", args.OrderBy(a => a.Parameter?.Ordinal ?? 0).Select(a => E(a.Value)));

    public string Args(IMethodSymbol m, IEnumerable<IArgumentOperation> args) => ArgList(args);

    string PropGet(IPropertyReferenceOperation pr)
    {
        var p = pr.Property;
        if (Decls.IsFieldLike(p))
        {
            if (p.IsStatic)
            {
                var s = CN(p.ContainingType) + "::" + Names.Id(p.Name);
                if (NeedsGuard(p.ContainingType)) return "(" + CN(p.ContainingType) + "::__cinit(), " + s + ")";
                return s;
            }
            return Recv(pr.Instance, p) + Names.Id(p.Name);
        }
        if (!Names.IsOurs(p)) NoteExternal(p);
        var getter = p.GetMethod ?? p.OverriddenProperty?.GetMethod;
        var gname = getter != null ? Names.MemberName(getter) : "get_" + p.Name;
        if (p.IsIndexer) gname = "get_Item";
        // primitive receivers
        if (pr.Instance != null && pr.Instance.Type != null && (Names.IsPrimitive(pr.Instance.Type) || pr.Instance.Type.TypeKind == TypeKind.Enum))
            return Owner(p.ContainingType) + "::" + gname + "(" + string.Join(", ", new[] { E(pr.Instance) }.Concat(pr.Arguments.OrderBy(a => a.Parameter.Ordinal).Select(a => E(a.Value)))) + ")";
        return Recv(pr.Instance, p) + gname + "(" + ArgList(pr.Arguments) + ")";
    }

    string PropSet(IPropertyReferenceOperation pr, string value)
    {
        var p = pr.Property;
        if (Decls.IsFieldLike(p)) return PropGet(pr) + " = " + value;
        var sname = p.IsIndexer ? "set_Item" : (p.SetMethod != null ? Names.MemberName(p.SetMethod) : "set_" + p.Name);
        var args = pr.Arguments.OrderBy(a => a.Parameter.Ordinal).Select(a => E(a.Value)).ToList();
        args.Add(value);
        return Recv(pr.Instance, p) + sname + "(" + string.Join(", ", args) + ")";
    }

    bool IsPropTarget(IOperation t, out IPropertyReferenceOperation pr)
    {
        pr = t as IPropertyReferenceOperation;
        return pr != null && !Decls.IsFieldLike(pr.Property);
    }

    string Assign(ISimpleAssignmentOperation a, bool stmt)
    {
        var val = E(a.Value);
        if (IsPropTarget(a.Target, out var pr))
        {
            if (stmt) return PropSet(pr, val);
            var t = Tmp("__v");
            return "([&]() { auto " + t + " = " + val + "; " + PropSet(pr, t) + "; return " + t + "; }())";
        }
        var s = E(a.Target) + " = " + val;
        return stmt ? s : "(" + s + ")";
    }

    string BinOpKind(BinaryOperatorKind k) => BinOp(k);

    string Compound(ICompoundAssignmentOperation ca, bool stmt)
    {
        var tt = ca.Target.Type;
        // delegate combine
        if (tt != null && tt.TypeKind == TypeKind.Delegate)
        {
            var fn = ca.OperatorKind == BinaryOperatorKind.Add ? "::sys::Delegate_Combine" : "::sys::Delegate_Remove";
            return AssignTo(ca.Target, fn + "(" + E(ca.Target) + ", " + E(ca.Value) + ")", stmt);
        }
        bool needsExpand = IsPropTarget(ca.Target, out _) || tt?.SpecialType == SpecialType.System_String || tt?.TypeKind == TypeKind.Enum
            || (ca.OperatorMethod != null) || (ca.OperatorKind == BinaryOperatorKind.Remainder && IsFloat(tt))
            || (tt != null && tt.OriginalDefinition.SpecialType == SpecialType.System_Nullable_T);
        if (!needsExpand)
        {
            var s = E(ca.Target) + " " + BinOp(ca.OperatorKind) + "= " + E(ca.Value);
            return stmt ? s : "(" + s + ")";
        }
        var cur = E(ca.Target);
        string combined = BinaryExpr(ca.OperatorKind, ca.OperatorMethod, ca.Target.Type, ca.Value.Type, cur, E(ca.Value), tt);
        if (tt?.TypeKind == TypeKind.Enum || Names.IsPrimitive(tt)) combined = "static_cast<" + T(tt) + ">(" + combined + ")";
        return AssignTo(ca.Target, combined, stmt);
    }

    string AssignTo(IOperation target, string value, bool stmt)
    {
        if (IsPropTarget(target, out var pr))
        {
            if (stmt) return PropSet(pr, value);
            var t = Tmp("__v");
            return "([&]() { auto " + t + " = " + value + "; " + PropSet(pr, t) + "; return " + t + "; }())";
        }
        var s = E(target) + " = " + value;
        return stmt ? s : "(" + s + ")";
    }

    string EventAssign(IEventAssignmentOperation ea)
    {
        var target = ea.EventReference as IEventReferenceOperation;
        var cur = E(ea.EventReference);
        var fn = ea.Adds ? "::sys::Delegate_Combine" : "::sys::Delegate_Remove";
        return cur + " = " + fn + "(" + cur + ", " + E(ea.HandlerValue) + ")";
    }

    string IncDec(IIncrementOrDecrementOperation inc, bool stmt)
    {
        bool isInc = inc.Kind == OperationKind.Increment;
        bool postfix = inc.IsPostfix;
        var tt = inc.Target.Type;
        if (IsPropTarget(inc.Target, out var pr) || tt.TypeKind == TypeKind.Enum || inc.OperatorMethod != null)
        {
            var cur = E(inc.Target);
            string next;
            if (inc.OperatorMethod != null) next = CN(inc.OperatorMethod.ContainingType) + "::" + Names.MemberName(inc.OperatorMethod) + "(" + cur + ")";
            else if (tt.TypeKind == TypeKind.Enum) next = "static_cast<" + T(tt) + ">(static_cast<int64_t>(" + cur + ") " + (isInc ? "+" : "-") + " 1)";
            else next = "static_cast<" + T(tt) + ">(" + cur + (isInc ? " + 1" : " - 1") + ")";
            if (stmt) return AssignTo(inc.Target, next, true);
            var t = Tmp("__old");
            var n = Tmp("__new");
            return "([&]() { auto " + t + " = " + cur + "; auto " + n + " = " + next.Replace(cur, t) + "; " + AssignToRaw(inc.Target, n) + "; return " + (postfix ? t : n) + "; }())";
        }
        var e = E(inc.Target);
        var op = isInc ? "++" : "--";
        return postfix ? (stmt ? e + op : "(" + e + op + ")") : (stmt ? op + e : "(" + op + e + ")");
    }

    string AssignToRaw(IOperation target, string value)
    {
        if (IsPropTarget(target, out var pr)) return PropSet(pr, value);
        return E(target) + " = " + value;
    }

    string CondAccess(IConditionalAccessOperation cao, bool stmt)
    {
        var v = Tmp("__ca");
        var saved = C.CondAccessVar;
        var recv = E(cao.Operation);
        C.CondAccessVar = v;
        string body = cao.WhenNotNull is IConditionalAccessOperation inner ? CondAccess(inner, stmt) : (stmt ? ES(cao.WhenNotNull) : E(cao.WhenNotNull));
        C.CondAccessVar = saved;
        bool nullableRecv = cao.Operation.Type != null && cao.Operation.Type.OriginalDefinition.SpecialType == SpecialType.System_Nullable_T;
        var test = nullableRecv ? v + ".get_HasValue()" : v + " != nullptr";
        if (stmt || cao.Type == null || cao.Type.SpecialType == SpecialType.System_Void)
            return "([&]() { auto " + v + " = " + recv + "; if (" + test + ") { " + body + "; } }())";
        return "([&]() -> " + T(cao.Type) + " { auto " + v + " = " + recv + "; if (" + test + ") return " + body + "; return " + Default(cao.Type) + "; }())";
    }

    static bool IsFloat(ITypeSymbol t) => t != null && (t.SpecialType == SpecialType.System_Single || t.SpecialType == SpecialType.System_Double);

    public static string BinOp(BinaryOperatorKind k) => k switch
    {
        BinaryOperatorKind.Add => "+", BinaryOperatorKind.Subtract => "-", BinaryOperatorKind.Multiply => "*", BinaryOperatorKind.Divide => "/",
        BinaryOperatorKind.Remainder => "%", BinaryOperatorKind.LeftShift => "<<", BinaryOperatorKind.RightShift => ">>",
        BinaryOperatorKind.And => "&", BinaryOperatorKind.Or => "|", BinaryOperatorKind.ExclusiveOr => "^",
        BinaryOperatorKind.ConditionalAnd => "&&", BinaryOperatorKind.ConditionalOr => "||",
        BinaryOperatorKind.Equals => "==", BinaryOperatorKind.NotEquals => "!=", BinaryOperatorKind.LessThan => "<",
        BinaryOperatorKind.LessThanOrEqual => "<=", BinaryOperatorKind.GreaterThan => ">", BinaryOperatorKind.GreaterThanOrEqual => ">=",
        BinaryOperatorKind.ObjectValueEquals => "==", BinaryOperatorKind.ObjectValueNotEquals => "!=",
        _ => "/*op " + k + "*/"
    };

    static string OpName(BinaryOperatorKind k) => k switch
    {
        BinaryOperatorKind.Add => "op_Addition", BinaryOperatorKind.Subtract => "op_Subtraction", BinaryOperatorKind.Multiply => "op_Multiply",
        BinaryOperatorKind.Divide => "op_Division", BinaryOperatorKind.Equals => "op_Equality", BinaryOperatorKind.NotEquals => "op_Inequality",
        _ => "op_" + k
    };

    string Binary(IBinaryOperation b) =>
        BinaryExpr(b.OperatorKind, b.OperatorMethod, b.LeftOperand.Type, b.RightOperand.Type, E(b.LeftOperand), E(b.RightOperand), b.Type, b);

    string BinaryExpr(BinaryOperatorKind k, IMethodSymbol opm, ITypeSymbol lt, ITypeSymbol rt, string l, string r, ITypeSymbol resT, IBinaryOperation b = null)
    {
        // string concatenation
        if (k == BinaryOperatorKind.Add && (lt?.SpecialType == SpecialType.System_String || rt?.SpecialType == SpecialType.System_String) && (resT?.SpecialType == SpecialType.System_String))
            return "::sys::concat(" + l + ", " + r + ")";
        if ((k == BinaryOperatorKind.Equals || k == BinaryOperatorKind.NotEquals) && lt?.SpecialType == SpecialType.System_String && rt?.SpecialType == SpecialType.System_String)
            return (k == BinaryOperatorKind.NotEquals ? "!" : "") + "::System::String::op_Equality(" + l + ", " + r + ")";
        if (opm != null)
        {
            var ct = opm.ContainingType;
            if (Names.IsOurs(opm) || ct.SpecialType == SpecialType.System_String || ct.IsReferenceType)
            {
                if (ct.SpecialType == SpecialType.System_String && (k == BinaryOperatorKind.Equals || k == BinaryOperatorKind.NotEquals))
                    return (k == BinaryOperatorKind.NotEquals ? "!" : "") + "::System::String::op_Equality(" + l + ", " + r + ")";
                if (!Names.IsOurs(opm)) NoteExternal(opm);
                return CN(ct) + "::" + Names.MemberName(opm) + "(" + l + ", " + r + ")";
            }
            NoteExternal(opm);
            return "(" + l + " " + BinOp(k) + " " + r + ")";
        }
        if (k == BinaryOperatorKind.Remainder && (IsFloat(lt) || IsFloat(rt)))
            return "::sys::fmod(" + l + ", " + r + ")";
        bool lEnum = lt?.TypeKind == TypeKind.Enum, rEnum = rt?.TypeKind == TypeKind.Enum;
        bool cmp = k is BinaryOperatorKind.Equals or BinaryOperatorKind.NotEquals or BinaryOperatorKind.LessThan or BinaryOperatorKind.LessThanOrEqual or BinaryOperatorKind.GreaterThan or BinaryOperatorKind.GreaterThanOrEqual;
        if ((lEnum || rEnum) && !(cmp && lEnum && rEnum))
        {
            var li = lEnum ? "static_cast<int64_t>(" + l + ")" : l;
            var ri = rEnum ? "static_cast<int64_t>(" + r + ")" : r;
            var e = "(" + li + " " + BinOp(k) + " " + ri + ")";
            if (cmp || resT == null) return e;
            return "static_cast<" + T(resT) + ">" + e;
        }
        // nullable lifted
        bool lNull = lt != null && lt.OriginalDefinition.SpecialType == SpecialType.System_Nullable_T;
        bool rNull = rt != null && rt.OriginalDefinition.SpecialType == SpecialType.System_Nullable_T;
        if (lNull || rNull)
        {
            return "(" + l + " " + BinOp(k) + " " + r + ")";
        }
        // reference comparisons between interface and class: cast to Object
        if ((k == BinaryOperatorKind.Equals || k == BinaryOperatorKind.NotEquals) && lt != null && rt != null && lt.IsReferenceType && rt.IsReferenceType
            && (Names.IsInterface(lt) != Names.IsInterface(rt)) && l != "nullptr" && r != "nullptr")
        {
            return "(::sys::to_object(" + l + ") " + BinOp(k) + " ::sys::to_object(" + r + "))";
        }
        if (k is BinaryOperatorKind.And or BinaryOperatorKind.Or or BinaryOperatorKind.ExclusiveOr && resT?.SpecialType == SpecialType.System_Boolean)
            return "((bool)(" + l + " " + BinOp(k) + " " + r + "))";
        return "(" + l + " " + BinOp(k) + " " + r + ")";
    }

    string Unary(IUnaryOperation u)
    {
        var o = E(u.Operand);
        if (u.OperatorMethod != null)
        {
            if (Names.IsOurs(u.OperatorMethod))
                return CN(u.OperatorMethod.ContainingType) + "::" + Names.MemberName(u.OperatorMethod) + "(" + o + ")";
        }
        string op = u.OperatorKind switch
        {
            UnaryOperatorKind.Not => "!",
            UnaryOperatorKind.Minus => "-",
            UnaryOperatorKind.Plus => "+",
            UnaryOperatorKind.BitwiseNegation => "~",
            UnaryOperatorKind.True => "",
            UnaryOperatorKind.False => "!",
            _ => "/*unop*/"
        };
        if (u.Operand.Type?.TypeKind == TypeKind.Enum && u.OperatorKind == UnaryOperatorKind.BitwiseNegation)
            return "static_cast<" + T(u.Operand.Type) + ">(~static_cast<int64_t>(" + o + "))";
        return "(" + op + o + ")";
    }

    string Conv(IConversionOperation cv)
    {
        var conv = cv.GetConversion();
        var from = cv.Operand.Type;
        var to = cv.Type;
        var o = E(cv.Operand);
        if (conv.IsIdentity) return o;
        if (from == null) // null literal or lambda etc
        {
            if (cv.Operand.ConstantValue.HasValue && cv.Operand.ConstantValue.Value == null) return Const(null, to);
            return o;
        }
        if (conv.IsUserDefined && conv.MethodSymbol != null)
        {
            var m = conv.MethodSymbol;
            var inner = o;
            var ptype = m.Parameters[0].Type;
            if (!SymbolEqualityComparer.Default.Equals(ptype, from) && Names.IsPointerLike(ptype) == false && Names.IsPrimitive(ptype)) inner = "static_cast<" + T(ptype) + ">(" + inner + ")";
            string res;
            if (Names.IsOurs(m)) res = CN(m.ContainingType) + "::" + Names.MemberName(m) + "(" + inner + ")";
            else { NoteExternal(m); res = "static_cast<" + T(m.ReturnType) + ">(" + inner + ")"; }
            if (!SymbolEqualityComparer.Default.Equals(m.ReturnType, to) && Names.IsPrimitive(to)) res = "static_cast<" + T(to) + ">(" + res + ")";
            return res;
        }
        if (conv.IsBoxing)
        {
            if (from.OriginalDefinition.SpecialType == SpecialType.System_Nullable_T) return "::sys::box_nullable(" + o + ")";
            if (from is ITypeParameterSymbol) return "::sys::box_any<" + T(from) + ">(" + o + ")";
            return "::sys::box<" + T(from) + ">(" + o + ")";
        }
        if (conv.IsUnboxing)
        {
            if (to is ITypeParameterSymbol) return "::sys::unbox_any<" + T(to) + ">(" + o + ")";
            return "::sys::unbox<" + T(to) + ">(" + o + ")";
        }
        if (conv.IsNullable)
        {
            if (to.OriginalDefinition.SpecialType == SpecialType.System_Nullable_T)
            {
                if (from.OriginalDefinition.SpecialType == SpecialType.System_Nullable_T) return T(to) + "(" + o + ")";
                var inner = ((INamedTypeSymbol)to).TypeArguments[0];
                var val = SymbolEqualityComparer.Default.Equals(inner, from) ? o : "static_cast<" + T(inner) + ">(" + o + ")";
                return T(to) + "(" + val + ")";
            }
            return "static_cast<" + T(to) + ">(" + Paren(o) + ".get_Value())";
        }
        if (conv.IsNumeric || conv.IsEnumeration || (Names.IsPrimitive(from) && Names.IsPrimitive(to)))
        {
            if (SymbolEqualityComparer.Default.Equals(from, to)) return o;
            return "static_cast<" + T(to) + ">(" + o + ")";
        }
        if (conv.IsReference)
        {
            if (from is ITypeParameterSymbol || to is ITypeParameterSymbol)
                return conv.IsImplicit ? "::sys::upcast<" + T(to) + ">(" + o + ")" : "::sys::cast<" + T(to) + ">(" + o + ")";
            if (conv.IsImplicit)
            {
                if (Names.IsInterface(from) && !Names.IsInterface(to)) return "::sys::to_object(" + o + ")";
                if (from.TypeKind == TypeKind.Delegate || to.TypeKind == TypeKind.Delegate) return "::sys::upcast<" + T(to) + ">(" + o + ")";
                if (from is IArrayTypeSymbol && to is not IArrayTypeSymbol) return "::sys::upcast<" + T(to) + ">(" + o + ")";
                if (from is IArrayTypeSymbol fa && to is IArrayTypeSymbol ta && !SymbolEqualityComparer.Default.Equals(fa.ElementType, ta.ElementType))
                    return "::sys::array_cast<" + T(to) + ">(" + o + ")";
                return o;
            }
            if (from is IArrayTypeSymbol || to is IArrayTypeSymbol) return "::sys::cast<" + T(to) + ">(" + o + ")";
            return "::sys::cast<" + T(to) + ">(" + o + ")";
        }
        if (conv.Exists && conv.IsImplicit) return o;
        if (from.TypeKind == TypeKind.Delegate || to.TypeKind == TypeKind.Delegate) return o;
        // fallback
        if (from.IsValueType && to.IsValueType) return "static_cast<" + T(to) + ">(" + o + ")";
        return "::sys::cast<" + T(to) + ">(" + o + ")";
    }

    // ===================== creation =====================
    string Create(IObjectCreationOperation oc)
    {
        var t = (INamedTypeSymbol)oc.Type;
        string baseExpr;
        if (t.TypeKind == TypeKind.Delegate)
        {
            var a = oc.Arguments.FirstOrDefault()?.Value;
            baseExpr = a != null ? E(a) : "nullptr";
        }
        else
        {
            var args = oc.Constructor != null ? Args(oc.Constructor, oc.Arguments) : "";
            if (!Names.IsOurs(t)) NoteExternal(oc.Constructor ?? (ISymbol)t);
            if (t.IsReferenceType && Names.IsOurs(t)) baseExpr = "(::sys::gcnew<" + CN(t) + ">())->__ctor(" + args + ")";
            else if (t.IsReferenceType) baseExpr = "::sys::gcnew<" + CN(t) + ">(" + args + ")";
            else baseExpr = CN(t) + "(" + args + ")";
        }
        if (oc.Initializer == null || oc.Initializer.Initializers.Length == 0) return baseExpr;
        var tmp = Tmp("__init");
        var sb = new StringBuilder();
        sb.Append("([&]() { auto " + tmp + " = " + baseExpr + "; ");
        C.ImplicitReceivers.Push(tmp);
        foreach (var i in oc.Initializer.Initializers)
        {
            if (i is ISimpleAssignmentOperation sa) sb.Append(Assign(sa, true) + "; ");
            else if (i is IMemberInitializerOperation mi) sb.Append("/*nested member init*/ ");
            else sb.Append(ES(i) + "; ");
        }
        C.ImplicitReceivers.Pop();
        sb.Append("return " + tmp + "; }())");
        return sb.ToString();
    }

    string ArrayCreate(IArrayCreationOperation ac)
    {
        var at = (IArrayTypeSymbol)ac.Type;
        var cls = CN(at);
        if (ac.Initializer != null)
        {
            if (at.Rank == 1)
                return cls + "::Of({" + string.Join(", ", ac.Initializer.ElementValues.Select(v => ElemValue(v, at.ElementType))) + "})";
            // multi-dim initializer
            var rows = ac.Initializer.ElementValues.Cast<IArrayInitializerOperation>().ToList();
            return cls + "::Of2({" + string.Join(", ", rows.Select(r => "{" + string.Join(", ", r.ElementValues.Select(v => ElemValue(v, at.ElementType))) + "}")) + "})";
        }
        return cls + "::New(" + string.Join(", ", ac.DimensionSizes.Select(E)) + ")";
    }

    string ElemValue(IOperation v, ITypeSymbol et)
    {
        var s = E(v);
        if (v.Type != null && !SymbolEqualityComparer.Default.Equals(v.Type, et) && Names.IsPointerLike(et)) s = "static_cast<" + T(et) + ">(" + s + ")";
        if (v.Type == null && Names.IsPointerLike(et)) s = "static_cast<" + T(et) + ">(" + s + ")";
        return s;
    }

    string DelegateCreate(IDelegateCreationOperation dc)
    {
        var dt = (INamedTypeSymbol)dc.Type;
        switch (dc.Target)
        {
            case IAnonymousFunctionOperation af: return Lambda(af, dt);
            case IMethodReferenceOperation mr:
            {
                var m = mr.Method;
                var inv = dt.DelegateInvokeMethod;
                var sig = Names.Sig(inv);
                var targs = m.IsGenericMethod ? "<" + string.Join(", ", m.TypeArguments.Select(T)) + ">" : "";
                if (m.IsStatic || mr.Instance == null)
                {
                    var fp = Names.Type(m.ReturnType) + "(*)(" + string.Join(", ", m.Parameters.Select(Names.ParamType)) + ")";
                    return "::sys::fn_static<" + sig + ">(static_cast<" + fp + ">(&" + CN(m.ContainingType) + "::" + Names.MemberName(m) + targs + "))";
                }
                var ct = m.ContainingType;
                var mp = Names.Type(m.ReturnType) + "(" + CN(ct) + "::*)(" + string.Join(", ", m.Parameters.Select(Names.ParamType)) + ")";
                string inst = mr.Instance is IInstanceReferenceOperation ? "this" : E(mr.Instance);
                if (mr.Instance != null && mr.Instance.Type != null && mr.Instance.Type.IsValueType)
                {
                    // bound to a struct value: capture copy
                    var cap = Tmp("__sv");
                    return "::sys::fn<" + sig + ">([" + cap + " = " + inst + "](" + string.Join(", ", inv.Parameters.Select((p, i) => Names.ParamType(p) + " a" + i)) + ") mutable { return " + cap + "." + Names.MemberName(m) + "(" + string.Join(", ", inv.Parameters.Select((p, i) => "a" + i)) + "); })";
                }
                return "::sys::fn_bind<" + sig + ">(" + inst + ", static_cast<" + mp + ">(&" + CN(ct) + "::" + Names.MemberName(m) + targs + "))";
            }
        }
        return E(dc.Target);
    }

    string Lambda(IAnonymousFunctionOperation af, INamedTypeSymbol dt)
    {
        var m = af.Symbol;
        var sig = dt != null ? Names.Sig(dt.DelegateInvokeMethod) : Names.Sig(m);
        var cap = C.InStaticContext || C.Method == null && C.InStaticContext ? "[=]" : "[=, this]";
        if (C.Type.IsValueType) cap = "[=]";
        if (C.InStaticContext) cap = "[=]";
        var savedStatic = C.InStaticContext;
        var sb = new StringBuilder();
        var ps = string.Join(", ", m.Parameters.Select(p => Names.ParamType(p) + " " + ParamName(p)));
        sb.Append("::sys::fn<" + sig + ">(" + cap + "(" + ps + ") mutable -> " + T(m.ReturnType) + " {\n");
        // boxed lambda params
        foreach (var p in m.Parameters)
            if (C.Boxed.Contains(p))
                sb.Append("auto* " + Names.Id(p.Name) + " = ::sys::gcnew<::sys::Ref<" + T(p.Type) + ">>(" + Names.Id(p.Name) + "__p);\n");
        var savedIter = C.IsIterator; C.IsIterator = false;
        var savedSw = C.SwitchExits; C.SwitchExits = new();
        sb.Append(Stmts(af.Body.Operations, 2));
        C.IsIterator = savedIter; C.SwitchExits = savedSw;
        sb.Append("})");
        return sb.ToString();
    }

    // ===================== invocation =====================
    static readonly HashSet<string> ObjectMethods = new() { "ToString", "Equals", "GetHashCode", "GetType", "CompareTo" };

    string Invoke(IInvocationOperation inv)
    {
        var m = inv.TargetMethod;
        var args = inv.Arguments.OrderBy(a => a.Parameter?.Ordinal ?? 0).Select(a => E(a.Value)).ToList();
        if (m.MethodKind == MethodKind.DelegateInvoke)
            return "::sys::nn(" + E(inv.Instance) + ")->Invoke(" + string.Join(", ", args) + ")";
        var name = Names.MemberName(m);
        var targs = m.IsGenericMethod ? "<" + string.Join(", ", m.TypeArguments.Select(T)) + ">" : "";
        var tkw = m.IsGenericMethod ? "template " : "";
        if (!Names.IsOurs(m)) NoteExternal(m);
        // Object-ish methods handled by runtime helpers for uniformity
        if (!m.IsStatic && ObjectMethods.Contains(m.Name) && inv.Instance != null && !(inv.Instance is IInstanceReferenceOperation ir0 && inv.Instance.Syntax is BaseExpressionSyntax))
        {
            var recvT = inv.Instance.Type;
            bool userRef = Names.IsOurs(m) && recvT != null && recvT.IsReferenceType;
            if (!userRef)
            {
                var recv = E(inv.Instance);
                return "::sys::" + m.Name + "(" + string.Join(", ", new[] { recv }.Concat(args)) + ")";
            }
        }
        if (m.IsStatic || inv.Instance == null)
        {
            var ct = m.ContainingType;
            if (m.IsExtensionMethod && m.ReducedFrom != null) { }
            return Owner(ct) + "::" + tkw + name + targs + "(" + string.Join(", ", args) + ")";
        }
        var it = inv.Instance.Type;
        if (it != null && (Names.IsPrimitive(it) || it.TypeKind == TypeKind.Enum) && !(inv.Instance is IInstanceReferenceOperation))
        {
            var ctn = it.TypeKind == TypeKind.Enum ? "::System::Enum" : Owner(m.ContainingType);
            return ctn + "::" + name + "(" + string.Join(", ", new[] { E(inv.Instance) }.Concat(args)) + ")";
        }
        return Recv(inv.Instance, m) + tkw + name + targs + "(" + string.Join(", ", args) + ")";
    }
}
