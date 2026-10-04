#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"

#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/Basic/SourceLocation.h>
#include <unordered_set>

#include "RefactorTool.h"

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;

static llvm::cl::OptionCategory ToolCategory("refactor-tool options");

/* ============================================================================================ */

// Метод run вызывается для каждого совпадения с матчем.
// Мы проверяем тип совпадения по bind-именам и применяем рефакторинг.
void RefactorHandler::run(const MatchFinder::MatchResult &Result) {
    auto has_derived = [](const CXXRecordDecl *base, ASTContext &Context) {
        const CXXRecordDecl *other = nullptr;
        for (const auto *Decl : Context.getTranslationUnitDecl()->decls()) {
            other = dyn_cast<CXXRecordDecl>(Decl);
            if (other && other != base && other->hasDefinition() && !other->isImplicit() &&
                other->isDerivedFrom(base)) {
                return true;
            }
        }
        return false;
    };

    auto &Diag = Result.Context->getDiagnostics();
    auto &SM = *Result.SourceManager;  // Получаем SourceManager для проверки isInMainFile

    if (const auto *Dtor = Result.Nodes.getNodeAs<CXXDestructorDecl>("classDecl")) {
        if (Dtor) {
            auto d_class = Dtor->getParent();
            if (!Dtor->isOutOfLine() && d_class->getNumBases() == 0 && has_derived(d_class, *(Result.Context)))
                handle_nv_dtor(Dtor, Diag, SM);
        }
    }

    if (const auto *Method = Result.Nodes.getNodeAs<CXXMethodDecl>("methodDecl");
        Method && Method->size_overridden_methods() > 0 && !Method->hasAttr<OverrideAttr>()) {
        handle_miss_override(Method, Diag, SM);
    }

    if (const auto *LoopVar = Result.Nodes.getNodeAs<VarDecl>("VarDecl")) {
        handle_crange_for(LoopVar, Diag, SM);
    }
}

void RefactorHandler::handle_nv_dtor(const CXXDestructorDecl *Dtor, DiagnosticsEngine &Diag, SourceManager &SM) {

    auto loc = Dtor->getBeginLoc();
    if (SM.isInSystemHeader(loc))
        return;

    if (!this->virtualDtorLocations.insert(loc.getRawEncoding()).second)
        return;

    Rewrite.InsertTextBefore(loc, "virtual ");

    const unsigned DiagID = Diag.getCustomDiagID(DiagnosticsEngine::Remark, "Set 'virtual' in destructor");
    Diag.Report(Dtor->getLocation(), DiagID);
}

void RefactorHandler::handle_miss_override(const CXXMethodDecl *Method, DiagnosticsEngine &Diag, SourceManager &SM) {
    auto body = Method->getBody();
    SourceLocation loc = (body) ? body->getBeginLoc() : Method->getEndLoc();

    if (SM.isInSystemHeader(loc))
        return;

    Rewrite.InsertTextAfter(loc, " override");

    const unsigned DiagID = Diag.getCustomDiagID(DiagnosticsEngine::Remark, "Set 'override' in method");
    Diag.Report(Method->getLocation(), DiagID);
}

void RefactorHandler::handle_crange_for(const VarDecl *LoopVar, DiagnosticsEngine &Diag, SourceManager &SM) {
    auto loc = LoopVar->getLocation();
    if (SM.isInSystemHeader(loc))
        return;

    if (LoopVar->getType()->isFundamentalType())
        return;

    Rewrite.InsertTextBefore(loc, "&");

    const unsigned DiagID = Diag.getCustomDiagID(DiagnosticsEngine::Remark, "Set '&' in const var in range-for");
    Diag.Report(LoopVar->getLocation(), DiagID);
}

/* ============================================================================================ */

auto NvDtorMatcher() { return cxxDestructorDecl(unless(isImplicit()), unless(isVirtualAsWritten())).bind("classDecl"); }

auto NoOverrideMatcher() {
    return cxxMethodDecl(unless(isImplicit()), isOverride(), unless(hasAttr(attr::Override)),
                         unless(cxxDestructorDecl()))
        .bind("methodDecl");
}

auto NoRefConstVarInRangeLoopMatcher() {
    return cxxForRangeStmt(hasLoopVariable(varDecl(hasType(isConstQualified()), unless(hasType(referenceType())),
                                                   unless(hasType(pointerType())), unless(hasType(builtinType())),
                                                   unless(hasType(enumType())))
                                               .bind("VarDecl")));
}

/* ============================================================================================ */

// Конструктор принимает Rewriter для изменения кода.
ComplexConsumer::ComplexConsumer(Rewriter &Rewrite) : Handler(Rewrite) {
    // Создаем MatchFinder и добавляем матчеры.
    Finder.addMatcher(NvDtorMatcher(), &Handler);
    Finder.addMatcher(NoOverrideMatcher(), &Handler);
    Finder.addMatcher(NoRefConstVarInRangeLoopMatcher(), &Handler);
}

// Метод HandleTranslationUnit вызывается для каждого файла.
void ComplexConsumer::HandleTranslationUnit(ASTContext &Context) { Finder.matchAST(Context); }

/* ============================================================================================ */

std::unique_ptr<ASTConsumer> CodeRefactorAction::CreateASTConsumer(CompilerInstance &CI, StringRef file) {
    RewriterForCodeRefactor.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
    return std::make_unique<ComplexConsumer>(RewriterForCodeRefactor);
}

bool CodeRefactorAction::BeginSourceFileAction(CompilerInstance &CI) {
    // Инициализируем Rewriter для рефакторинга.
    RewriterForCodeRefactor.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
    return true;  // Возвращаем true, чтобы продолжить обработку файла.
}

void CodeRefactorAction::EndSourceFileAction() {
    // Применяем изменения в файле.
    if (RewriterForCodeRefactor.overwriteChangedFiles()) {
        llvm::errs() << "Error applying changes to files.\n";
    }
}

/* ============================================================================================ */

int main(int argc, const char **argv) {
    // Парсер опций: Обрабатывает флаги командной строки, компиляционные базы данных.
    auto ExpectedParser = CommonOptionsParser::create(argc, argv, ToolCategory);
    if (!ExpectedParser) {
        llvm::errs() << ExpectedParser.takeError();
        return 1;
    }
    CommonOptionsParser &OptionsParser = ExpectedParser.get();
    // Создаем ClangTool
    ClangTool Tool(OptionsParser.getCompilations(), OptionsParser.getSourcePathList());
    // Запускаем RefactorAction.
    return Tool.run(newFrontendActionFactory<CodeRefactorAction>().get());
}