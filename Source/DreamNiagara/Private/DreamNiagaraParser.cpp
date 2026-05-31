#include "DreamNiagaraParser.h"

#include "DreamNiagaraParserInternal.h"

namespace UE::DreamNiagara
{
	namespace
	{
		bool ParseAssignmentsBlock(
			const FString& Block,
			TArray<FDreamNiagaraAssignment>& OutAssignments,
			FString& OutError)
		{
			Private::FDnsScanner Scanner(Block);
			while (true)
			{
				Scanner.SkipIgnored();
				if (Scanner.IsAtEnd())
				{
					return true;
				}

				FDreamNiagaraAssignment Assignment;
				Assignment.Line = Scanner.Line;
				if (Scanner.Peek() == TCHAR('"'))
				{
					if (!Scanner.ParsePathOrString(Assignment.Name, OutError))
					{
						return false;
					}
				}
				else if (!Scanner.ParseDottedIdentifier(Assignment.Name, OutError))
				{
					return false;
				}

				if (!Scanner.Expect(TCHAR('='), OutError))
				{
					return false;
				}

				if (!Scanner.ParseAssignmentValue(Assignment.Value, OutError))
				{
					return false;
				}

				OutAssignments.Add(MoveTemp(Assignment));
			}
		}

		bool ParseUserBlock(
			const FString& Block,
			TArray<FDreamNiagaraUserParameter>& OutParameters,
			FString& OutError)
		{
			Private::FDnsScanner Scanner(Block);
			while (true)
			{
				Scanner.SkipIgnored();
				if (Scanner.IsAtEnd())
				{
					return true;
				}

				FDreamNiagaraUserParameter Parameter;
				Parameter.Line = Scanner.Line;
				if (!Scanner.ParseDottedIdentifier(Parameter.Name, OutError))
				{
					return false;
				}

				if (!Scanner.Expect(TCHAR(':'), OutError))
				{
					return false;
				}

				if (!Scanner.ParseIdentifier(Parameter.Type, OutError))
				{
					return false;
				}

				if (Scanner.TryConsume(TCHAR('=')))
				{
					Parameter.bHasDefaultValue = true;
					if (!Scanner.ParseAssignmentValue(Parameter.DefaultValue, OutError))
					{
						return false;
					}
				}
				else
				{
					Scanner.TryConsume(TCHAR(';'));
				}

				OutParameters.Add(MoveTemp(Parameter));
			}
		}

		bool ParseStackBody(const FString& Block, FDreamNiagaraStack& OutStack, FString& OutError)
		{
			Private::FDnsScanner Scanner(Block);
			while (true)
			{
				Scanner.SkipIgnored();
				if (Scanner.IsAtEnd())
				{
					return true;
				}

				if (!Scanner.ExpectKeyword(TEXT("use"), OutError))
				{
					return false;
				}

				FDreamNiagaraModuleCall ModuleCall;
				ModuleCall.Line = Scanner.Line;
				if (Scanner.Peek() == TCHAR('"') || Scanner.Peek() == TCHAR('/'))
				{
					if (!Scanner.ParsePathOrString(ModuleCall.ModuleId, OutError))
					{
						return false;
					}
				}
				else if (!Scanner.ParseDottedIdentifier(ModuleCall.ModuleId, OutError))
				{
					return false;
				}

				FString ModuleBlock;
				if (!Scanner.ExtractBalancedBlock(ModuleBlock, OutError))
				{
					return false;
				}

				if (!ParseAssignmentsBlock(ModuleBlock, ModuleCall.Inputs, OutError))
				{
					return false;
				}

				OutStack.Modules.Add(MoveTemp(ModuleCall));
				Scanner.TryConsume(TCHAR(';'));
			}
		}

		bool ParseEmitterBody(const FString& Block, FDreamNiagaraEmitter& OutEmitter, FString& OutError)
		{
			Private::FDnsScanner Scanner(Block);
			while (true)
			{
				Scanner.SkipIgnored();
				if (Scanner.IsAtEnd())
				{
					return true;
				}

				if (Scanner.TryConsumeKeyword(TEXT("stack")))
				{
					FDreamNiagaraStack Stack;
					Stack.Line = Scanner.Line;
					if (!Scanner.ParseIdentifier(Stack.Name, OutError))
					{
						return false;
					}

					FString StackBlock;
					if (!Scanner.ExtractBalancedBlock(StackBlock, OutError))
					{
						return false;
					}

					if (!ParseStackBody(StackBlock, Stack, OutError))
					{
						return false;
					}

					OutEmitter.Stacks.Add(MoveTemp(Stack));
					Scanner.TryConsume(TCHAR(';'));
					continue;
				}

				if (Scanner.TryConsumeKeyword(TEXT("renderer")))
				{
					FDreamNiagaraRenderer Renderer;
					Renderer.Line = Scanner.Line;
					if (!Scanner.ParseIdentifier(Renderer.Type, OutError))
					{
						return false;
					}

					FString RendererBlock;
					if (!Scanner.ExtractBalancedBlock(RendererBlock, OutError))
					{
						return false;
					}

					if (!ParseAssignmentsBlock(RendererBlock, Renderer.Properties, OutError))
					{
						return false;
					}

					OutEmitter.Renderers.Add(MoveTemp(Renderer));
					Scanner.TryConsume(TCHAR(';'));
					continue;
				}

				OutError = Scanner.BuildError(TEXT("Expected 'stack' or 'renderer' in emitter block."));
				return false;
			}
		}

		bool ParseSystemBody(const FString& Block, FDreamNiagaraSystem& OutSystem, FString& OutError)
		{
			Private::FDnsScanner Scanner(Block);
			while (true)
			{
				Scanner.SkipIgnored();
				if (Scanner.IsAtEnd())
				{
					return true;
				}

				if (Scanner.TryConsumeKeyword(TEXT("user")))
				{
					FString UserBlock;
					if (!Scanner.ExtractBalancedBlock(UserBlock, OutError))
					{
						return false;
					}

					if (!ParseUserBlock(UserBlock, OutSystem.UserParameters, OutError))
					{
						return false;
					}

					Scanner.TryConsume(TCHAR(';'));
					continue;
				}

				if (Scanner.TryConsumeKeyword(TEXT("emitter")))
				{
					FDreamNiagaraEmitter Emitter;
					Emitter.Line = Scanner.Line;
					if (!Scanner.ParseIdentifier(Emitter.Name, OutError))
					{
						return false;
					}

					FString SimTargetText;
					if (!Scanner.ParseIdentifier(SimTargetText, OutError))
					{
						return false;
					}
					if (SimTargetText.Equals(TEXT("cpu"), ESearchCase::IgnoreCase))
					{
						Emitter.SimTarget = EDreamNiagaraSimTarget::Cpu;
					}
					else if (SimTargetText.Equals(TEXT("gpu"), ESearchCase::IgnoreCase))
					{
						Emitter.SimTarget = EDreamNiagaraSimTarget::Gpu;
					}
					else
					{
						OutError = Scanner.BuildError(FString::Printf(TEXT("Unsupported emitter sim target '%s'."), *SimTargetText));
						return false;
					}

					FString EmitterBlock;
					if (!Scanner.ExtractBalancedBlock(EmitterBlock, OutError))
					{
						return false;
					}

					if (!ParseEmitterBody(EmitterBlock, Emitter, OutError))
					{
						return false;
					}

					OutSystem.Emitters.Add(MoveTemp(Emitter));
					Scanner.TryConsume(TCHAR(';'));
					continue;
				}

				OutError = Scanner.BuildError(TEXT("Expected 'user' or 'emitter' in system block."));
				return false;
			}
		}
	}

	bool FDreamNiagaraParser::ParseSystem(const FString& SourceText, FDreamNiagaraSystem& OutSystem, FString& OutError)
	{
		OutSystem = FDreamNiagaraSystem();

		Private::FDnsScanner Scanner(SourceText);
		if (!Scanner.ExpectKeyword(TEXT("System"), OutError))
		{
			return false;
		}

		OutSystem.Line = Scanner.Line;
		if (!Scanner.ParseIdentifier(OutSystem.Name, OutError))
		{
			return false;
		}

		if (!Scanner.Expect(TCHAR('-'), OutError) || !Scanner.Expect(TCHAR('>'), OutError))
		{
			return false;
		}

		if (!Scanner.ParsePathOrString(OutSystem.Root, OutError))
		{
			return false;
		}

		FString SystemBlock;
		if (!Scanner.ExtractBalancedBlock(SystemBlock, OutError))
		{
			return false;
		}

		if (!ParseSystemBody(SystemBlock, OutSystem, OutError))
		{
			return false;
		}

		Scanner.SkipIgnored();
		if (!Scanner.IsAtEnd())
		{
			OutError = Scanner.BuildError(TEXT("Unexpected tokens after system block."));
			return false;
		}

		if (OutSystem.Name.IsEmpty())
		{
			OutError = TEXT("System name cannot be empty.");
			return false;
		}

		if (OutSystem.Root.IsEmpty())
		{
			OutError = TEXT("System output root cannot be empty.");
			return false;
		}

		return true;
	}
}
