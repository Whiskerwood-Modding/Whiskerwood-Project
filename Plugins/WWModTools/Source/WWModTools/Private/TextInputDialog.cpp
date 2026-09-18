#include "TextInputDialog.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Framework/Application/SlateApplication.h"
#include "Editor.h"

void STextInputDialog::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SBox).MinDesiredWidth(320.f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 8.f, 8.f, 4.f)
			[
				SNew(STextBlock).Text(InArgs._Label)
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 0.f, 8.f, 8.f)
			[
				SAssignNew(TextBox, SEditableTextBox)
				.Text(InArgs._InitialText)
				.SelectAllTextWhenFocused(true)
				.OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type Type)
				{
					if (Type == ETextCommit::OnEnter && IsOKEnabled())
					{
						OnOKClicked();
					}
				})
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 0.f, 8.f, 8.f)
			[
				SNew(SUniformGridPanel).SlotPadding(FMargin(4.f, 0.f))
				+ SUniformGridPanel::Slot(0, 0)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("OK")))
					.IsEnabled_Raw(this, &STextInputDialog::IsOKEnabled)
					.OnClicked_Raw(this, &STextInputDialog::OnOKClicked)
				]
				+ SUniformGridPanel::Slot(1, 0)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Cancel")))
					.OnClicked_Raw(this, &STextInputDialog::OnCancelClicked)
				]
			]
		]
	];
}

bool STextInputDialog::ShowModal(
	const FText& Title, const FText& Label, const FString& Initial, FString& OutText)
{
	TSharedRef<STextInputDialog> Content = SNew(STextInputDialog)
		.Label(Label)
		.InitialText(FText::FromString(Initial));

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(Title)
		.SizingRule(ESizingRule::Autosized)
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		[
			Content
		];

	Content->ParentWindow = Window;
	Window->SetWidgetToFocusOnActivate(Content->TextBox);

	GEditor->EditorAddModalWindow(Window);

	if (!Content->bConfirmed) return false;
	OutText = Content->Result;
	return true;
}

FReply STextInputDialog::OnOKClicked()
{
	Result = TextBox->GetText().ToString().TrimStartAndEnd();
	bConfirmed = true;
	if (ParentWindow.IsValid())
	{
		ParentWindow.Pin()->RequestDestroyWindow();
	}
	return FReply::Handled();
}

FReply STextInputDialog::OnCancelClicked()
{
	if (ParentWindow.IsValid())
	{
		ParentWindow.Pin()->RequestDestroyWindow();
	}
	return FReply::Handled();
}

bool STextInputDialog::IsOKEnabled() const
{
	return TextBox.IsValid() && !TextBox->GetText().ToString().TrimStartAndEnd().IsEmpty();
}
