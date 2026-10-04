#include <clang/AST/ASTContext.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/DiagnosticOptions.h>
#include <clang/Frontend/ASTUnit.h>
#include <clang/Rewrite/Core/Rewriter.h>
#include <clang/Tooling/Tooling.h>
#include <gtest/gtest.h>

#include "../include/RefactorTool.h"

#include <iostream>

using namespace clang;

static llvm::IntrusiveRefCntPtr<DiagnosticIDs> DiagIDs(new DiagnosticIDs());

/* ============================================== HELPERS FOR DTOR =============================================== */

const CXXDestructorDecl *findFirstDtor(ASTContext &Ctx) {
    class Finder : public RecursiveASTVisitor<Finder> {
    public:
        const CXXDestructorDecl *Result = nullptr;
        bool VisitCXXDestructorDecl(CXXDestructorDecl *D) {
            if (!Result)
                Result = D;
            return true;
        }
    } F;
    F.TraverseDecl(Ctx.getTranslationUnitDecl());
    return F.Result;
}

std::string refactorNvDtor(const std::string &Code) {
    auto AST = tooling::buildASTFromCodeWithArgs(Code, {"-std=c++23"});
    if (!AST)
        return Code;

    ASTContext &Ctx = AST->getASTContext();
    SourceManager &SM = Ctx.getSourceManager();

    const CXXDestructorDecl *Dtor = findFirstDtor(Ctx);
    if (!Dtor)
        return Code;

    Rewriter Rewrite;
    Rewrite.setSourceMgr(SM, Ctx.getLangOpts());

    DiagnosticsEngine &Diag = AST->getDiagnostics();
    Diag.setClient(new IgnoringDiagConsumer(), true);

    RefactorHandler Handler(Rewrite);
    Handler.handle_nv_dtor(Dtor, Diag, SM);

    std::string Result;
    llvm::raw_string_ostream OS(Result);
    Rewrite.getEditBuffer(SM.getMainFileID()).write(OS);
    OS.flush();
    return Result;
}

/* ============================================== HELPERS FOR OVER =============================================== */

const CXXMethodDecl *findFirstOverrideCandidate(ASTContext &Ctx) {
    class Finder : public RecursiveASTVisitor<Finder> {
    public:
        const CXXMethodDecl *Result = nullptr;
        bool VisitCXXMethodDecl(CXXMethodDecl *M) {
            if (Result)
                return true;
            if (isa<CXXConstructorDecl>(M) || isa<CXXDestructorDecl>(M))
                return true;
            if (M->size_overridden_methods() == 0)
                return true;
            if (M->hasAttr<OverrideAttr>())
                return true;
            Result = M;
            return true;
        }
    } F;
    F.TraverseDecl(Ctx.getTranslationUnitDecl());
    return F.Result;
}

std::string refactorOverride(const std::string &Code) {
    auto AST = tooling::buildASTFromCodeWithArgs(Code, {"-std=c++23"});
    if (!AST)
        return Code;

    ASTContext &Ctx = AST->getASTContext();
    SourceManager &SM = Ctx.getSourceManager();

    const auto *Method = findFirstOverrideCandidate(Ctx);
    if (!Method)
        return Code;

    Rewriter Rewrite;
    Rewrite.setSourceMgr(SM, Ctx.getLangOpts());

    DiagnosticsEngine &Diag = AST->getDiagnostics();
    Diag.setClient(new IgnoringDiagConsumer(), true);

    RefactorHandler Handler(Rewrite);
    Handler.handle_miss_override(Method, Diag, SM);

    std::string Result;
    llvm::raw_string_ostream OS(Result);
    Rewrite.getEditBuffer(SM.getMainFileID()).write(OS);
    OS.flush();
    return Result;
}

/* =============================================== HELPERS FOR FOR =============================================== */

const VarDecl *findFirstRangeForCandidate(ASTContext &Ctx) {
    class Finder : public RecursiveASTVisitor<Finder> {
    public:
        const VarDecl *Result = nullptr;
        bool VisitCXXForRangeStmt(CXXForRangeStmt *S) {
            if (Result)
                return true;

            const VarDecl *Var = S->getLoopVariable();
            if (!Var)
                return true;

            QualType QT = Var->getType();
            if (!QT.isConstQualified())
                return true;
            if (QT->isReferenceType())
                return true;
            if (QT->isPointerType())
                return true;
            if (QT->isBuiltinType())
                return true;
            if (QT->isEnumeralType())
                return true;

            Result = Var;
            return true;
        }
    } F;
    F.TraverseDecl(Ctx.getTranslationUnitDecl());
    return F.Result;
}

std::string refactorRangeFor(const std::string &Code) {
    auto AST = tooling::buildASTFromCodeWithArgs(Code, {"-std=c++23"});
    if (!AST)
        return Code;

    ASTContext &Ctx = AST->getASTContext();
    SourceManager &SM = Ctx.getSourceManager();

    const auto *Var = findFirstRangeForCandidate(Ctx);
    if (!Var)
        return Code;

    Rewriter Rewrite;
    Rewrite.setSourceMgr(SM, Ctx.getLangOpts());

    DiagnosticsEngine &Diag = AST->getDiagnostics();
    Diag.setClient(new IgnoringDiagConsumer(), true);

    RefactorHandler Handler(Rewrite);
    Handler.handle_crange_for(Var, Diag, SM);

    std::string Result;
    llvm::raw_string_ostream OS(Result);
    Rewrite.getEditBuffer(SM.getMainFileID()).write(OS);
    OS.flush();
    return Result;
}

/* =============================================================================================================== */

// Integration test with check mathcers released with using 'check_refactor.sh'
TEST(HandleNvDtor, SimpleClass) {
    std::string Input = "class Base { public: ~Base(); };\n";
    std::string Expected = "class Base { public: virtual ~Base(); };\n";
    EXPECT_EQ(refactorNvDtor(Input), Expected);
}

TEST(HandleNvDtor, DefaultedDtor) {
    std::string Input = "class Base { public: ~Base() = default; };\n";
    std::string Expected = "class Base { public: virtual ~Base() = default; };\n";
    EXPECT_EQ(refactorNvDtor(Input), Expected);
}

TEST(HandleNvDtor, NoDtorNoChange) {
    std::string Input = "class Base {};\n";
    EXPECT_EQ(refactorNvDtor(Input), Input);
}

/* =============================================================================================================== */

// Integration test with check mathcers released with using 'check_refactor.sh'
TEST(HandleOverride, SimpleOverride) {
    std::string Input = "struct B { virtual void f() {} };\n"
                        "struct D : B { void f(){} };\n";
    std::string Expected = "struct B { virtual void f() {} };\n"
                           "struct D : B { void f() override{} };\n";
    EXPECT_EQ(refactorOverride(Input), Expected);
}

TEST(HandleOverride, AlreadyHasOverride) {
    std::string Input = "struct B { virtual void f() {} };\n"
                        "struct D : B { void f() override{}};\n";
    EXPECT_EQ(refactorOverride(Input), Input);
}

TEST(HandleOverride, HidingNotModified) {
    std::string Input = "struct B { void f(); };\n"
                        "struct D : B { void f(); };\n";
    EXPECT_EQ(refactorOverride(Input), Input);
}

/* =============================================================================================================== */

// Integration test with check mathcers released with using 'check_refactor.sh'
TEST(HandleCRangeFor, CustomHeavyType) {
    std::string Input = R"(
#include <iostream>
#include <string>
#include <vector>
struct CustomType {int id;std::string name;};
void f() {
    std::vector<CustomType> vec = {{1, "a"}, {2, "b"}};
    for (const auto h : vec) {
        (void)h;
    }
}
)";
    std::string Expected = R"(
#include <iostream>
#include <string>
#include <vector>
struct CustomType {int id;std::string name;};
void f() {
    std::vector<CustomType> vec = {{1, "a"}, {2, "b"}};
    for (const auto &h : vec) {
        (void)h;
    }
}
)";
    EXPECT_EQ(refactorRangeFor(Input), Expected);
}

TEST(HandleCRangeFor, BuiltinNotModified) {
    std::string Input = R"(
#include <iostream>
#include <vector>
void f() {
    std::vector<int> vec = {1, 2};
    for (const int x : vec) {
        (void)x;
    }
}
)";
    EXPECT_EQ(refactorRangeFor(Input), Input);
}

TEST(HandleCRangeFor, AlreadyReferenceNotModified) {
    std::string Input = R"(
#include <iostream>
#include <string>
#include <vector>
struct CustomType {int id;std::string name;};
void f() {
    std::vector<CustomType> vec = {{1, "a"}, {2, "b"}};
    for (const auto &h : vec) {
        (void)h;
    }
}
)";
    EXPECT_EQ(refactorRangeFor(Input), Input);
}