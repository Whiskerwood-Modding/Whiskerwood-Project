#include "NewModDialog.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Framework/Application/SlateApplication.h"

void SNewModDialog::Construct(const FArguments& InArgs)
{
	// Kept so fields the prompt does not show, such as owned fields and anything the author added by hand, survive a save
	Result = InArgs._InitialInfo;

	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox)

		+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 8.f, 8.f, 4.f)
		[
			SNew(STextBlock).Text(FText::FromString(TEXT("Mod name (letters and digits only, no spaces):")))
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 0.f, 8.f, 8.f)
		[
			SAssignNew(NameBox, SEditableTextBox)
			.HintText(FText::FromString(TEXT("MyAwesomeMod")))
			.Text(FText::FromString(InArgs._InitialInfo.FolderName))
			.IsReadOnly(InArgs._bLockName)
			.OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type Type)
			{
				if (Type == ETextCommit::OnEnter && IsOKEnabled())
				{
					OnOKClicked();
				}
			})
		];

	// Rows come straight from the field definitions, so a new field shows up here without touching this dialog
	for (const FModFieldDef& Def : ModActions::GetModFieldDefs())
	{
		if (!Def.bUserEditable) continue;

		Body->AddSlot().AutoHeight()
		[
			MakeField(Def, InArgs._InitialInfo.Get(Def.Key))
		];
	}

	Body->AddSlot().AutoHeight().Padding(8.f, 0.f, 8.f, 8.f)
	[
		SNew(SUniformGridPanel).SlotPadding(FMargin(4.f, 0.f))
		+ SUniformGridPanel::Slot(0, 0)
		[
			SNew(SButton)
			.Text(FText::FromString(InArgs._bLockName ? TEXT("Save") : TEXT("Create")))
			.IsEnabled_Raw(this, &SNewModDialog::IsOKEnabled)
			.OnClicked_Raw(this, &SNewModDialog::OnOKClicked)
		]
		+ SUniformGridPanel::Slot(1, 0)
		[
			SNew(SButton)
			.Text(FText::FromString(TEXT("Cancel")))
			.OnClicked_Raw(this, &SNewModDialog::OnCancelClicked)
		]
	];

	ChildSlot
	[
		SNew(SBox).MinDesiredWidth(360.f)
		[
			Body
		]
	];
}

TSharedRef<SWidget> SNewModDialog::MakeField(const FModFieldDef& Def, const FString& InitialValue)
{
	TSharedPtr<SEditableTextBox> Box;

	TSharedRef<SWidget> Row = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 0.f, 8.f, 4.f)
		[
			SNew(STextBlock).Text(FText::FromString(Def.Label + TEXT(":")))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 0.f, 8.f, 8.f)
		[
			SAssignNew(Box, SEditableTextBox)
			.HintText(FText::FromString(Def.ExampleValue))
			.Text(FText::FromString(InitialValue))
		];

	FieldBoxes.Add(Def.Key, Box);
	return Row;
}

bool SNewModDialog::ShowModal(FModInfo& InOutInfo, const FText& Title, bool bLockName)
{
	TSharedRef<SNewModDialog> Content = SNew(SNewModDialog)
		.InitialInfo(InOutInfo)
		.bLockName(bLockName);

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(Title)
		.SizingRule(ESizingRule::Autosized)
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		[
			Content
		];

	Content->ParentWindow = Window;

	GEditor->EditorAddModalWindow(Window);

	if (!Content->bConfirmed) return false;
	InOutInfo = Content->Result;
	return true;
}

FReply SNewModDialog::OnOKClicked()
{
	Result.FolderName = NameBox->GetText().ToString().TrimStartAndEnd();

	for (const TPair<FString, TSharedPtr<SEditableTextBox>>& Pair : FieldBoxes)
	{
		if (Pair.Value.IsValid())
		{
			Result.Set(Pair.Key, Pair.Value->GetText().ToString().TrimStartAndEnd());
		}
	}

	ModActions::ApplyFieldDefaults(Result);

	bConfirmed = true;
	if (ParentWindow.IsValid())
	{
		ParentWindow.Pin()->RequestDestroyWindow();
	}
	return FReply::Handled();
}

FReply SNewModDialog::OnCancelClicked()
{
	if (ParentWindow.IsValid())
	{
		ParentWindow.Pin()->RequestDestroyWindow();
	}
	return FReply::Handled();
}

bool SNewModDialog::IsOKEnabled() const
{
	if (!NameBox.IsValid()) return false;
	const FString Text = NameBox->GetText().ToString().TrimStartAndEnd();
	if (Text.IsEmpty()) return false;
	for (TCHAR Ch : Text)
	{
		if (!FChar::IsAlnum(Ch) && Ch != TEXT('_')) return false;
	}
	return true;
}
